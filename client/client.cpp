#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <chrono>
#include <bitset>
#include <iostream>


// ran on a mac system;


//#define SERVER_PORT 8080
#define MAX_BUFFER_SIZE 1024


using namespace std::chrono;

std::tm t{};

typedef struct {
    uint64_t request_id;
    uint64_t payload_len;
    uint32_t op_code;
} Req;

typedef struct {
    uint64_t request_id;
    uint64_t payload_len;
    uint32_t status_code;
} Rep;


static inline std::chrono::system_clock::time_point get_snowflake_epoch() {
    // Twitter Snowflake epoch: 2010-11-04 13:42:54 UTC
    // (tm is in local time, so be mindful of time zones on your system if you want exact UTC).
    std::tm epoch_tm = {};
    epoch_tm.tm_year = 110; // years since 1900 -> 2010
    epoch_tm.tm_mon  = 10;  // November (0-based: 0=Jan, 10=Nov)
    epoch_tm.tm_mday = 4;
    epoch_tm.tm_hour = 13;
    epoch_tm.tm_min  = 42;
    epoch_tm.tm_sec  = 54;
    epoch_tm.tm_isdst = -1;

    std::time_t epoch_time = std::mktime(&epoch_tm);
    return std::chrono::system_clock::from_time_t(epoch_time);
}

// 2) Return the current time in milliseconds relative to the Snowflake epoch
static inline uint64_t current_snowflake_millis() {
    using namespace std::chrono;
    static const system_clock::time_point epoch = get_snowflake_epoch();

    system_clock::time_point now = system_clock::now();
    uint64_t diff_ms = duration_cast<milliseconds>(now - epoch).count();
    return diff_ms;
}

// 3) next_id() function: produces a 64-bit ID according to Snowflake’s bit layout
uint64_t next_id() {
    // Hardcode some example dataCenter and machine IDs (each up to 31)
    static const uint8_t data_center_id = 1; // range: 0..31
    static const uint8_t machine_id     = 3; // range: 0..31
    static const uint16_t MAX_SEQUENCE  = (1 << 12) - 1;
    static uint64_t last_timestamp = 0;
    static uint16_t sequence       = 0;
    uint64_t current_timestamp = current_snowflake_millis();

    // If time has moved backwards, you have a clock issue (or you need custom logic)
    if (current_timestamp < last_timestamp) {
        throw std::runtime_error("System clock moved backwards!");
    }

    if (current_timestamp == last_timestamp) {
        // Same millisecond as last time => increment sequence
        sequence = (sequence + 1) & MAX_SEQUENCE;
        if (sequence == 0) {
            // We’ve overflowed the sequence for this millisecond => wait until next ms
            while ((current_timestamp = current_snowflake_millis()) <= last_timestamp) {
                // busy-wait or sleep
            }
        }
    } else {
        // New millisecond => reset sequence
        sequence = 0;
    }

    last_timestamp = current_timestamp;

    // Build the 64-bit Snowflake ID:
    //  41 bits: current_timestamp
    //   5 bits: data_center_id
    //   5 bits: machine_id
    //  12 bits: sequence

    // Safety check: make sure current_timestamp fits into 41 bits
    if (current_timestamp >= (1ULL << 41)) {
        throw std::overflow_error("Timestamp exceeds 41-bit limit!");
    }

    uint64_t id = 0;
    id |= (current_timestamp & 0x1FFFFFFFFFF) << 22; // 41 bits shift left 22
    id |= (static_cast<uint64_t>(data_center_id) & 0x1F) << 17;
    id |= (static_cast<uint64_t>(machine_id) & 0x1F) << 12;
    id |= (sequence & 0xFFF);

    return id;
}


void set_epoch() {
    // according to twitter's snowflake configurations
    t.tm_year = 110;
    t.tm_mon = 10;
    t.tm_mday = 4;
    t.tm_hour = 13;
    t.tm_min = 42;
    t.tm_sec = 54;
}

//    char const* msg = "LT1%M%T"; // this is for op_code1
//    char const* msg = "LT1%M%2%1000"; // this is for op_code2
//    char const* msg = "1891416443040444417%30"; // opcode 3
//char const* msg = "LT1%2"; // opcode 4

void send_request(int sock_fd, struct sockaddr_in server_addr, std::string request_msg, uint32_t op_code) {
    char buffer[MAX_BUFFER_SIZE];
    Req req_header;
    req_header.request_id = next_id();
    req_header.op_code = op_code;
    req_header.payload_len = request_msg.size() + 1;

    memcpy(buffer + 1, &req_header, sizeof(req_header));
    memcpy(buffer + sizeof(Req) + 1, request_msg.c_str(), req_header.payload_len);
    buffer[0] = '1';

    uint64_t total_length = sizeof(req_header) + req_header.payload_len + 1;
    buffer[total_length] = '\0';

    if (total_length > MAX_BUFFER_SIZE) {
        std::cerr << "Message too large!\n";
        return;
    }

    socklen_t server_len = sizeof(server_addr);
    int max_retries = 3; // Maximum number of retries
    int attempt = 0;
    bool received_response = false;

    while (attempt < max_retries) {
        attempt++;
        std::cout << "Attempt " << attempt << " - Sending request: " << request_msg
                  << " (op_code: " << op_code << ")\n";

        ssize_t sent_bytes = sendto(sock_fd, buffer, total_length, 0,
                                    (struct sockaddr*)&server_addr, sizeof(server_addr));
        if (sent_bytes < 0) {
            perror("sendto");
            return;
        }

        // Set timeout for response
        struct timeval tv;
        tv.tv_sec = 2; // 2 seconds timeout
        tv.tv_usec = 0;
        setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

        if (op_code == 4) {
            while (true) {
                ssize_t bytes_read = recvfrom(sock_fd, buffer, sizeof(buffer), 0,
                                              (struct sockaddr*)&server_addr, &server_len);
                if (bytes_read > 0) {
                    received_response = true;
                    buffer[bytes_read] = '\0';
                    std::string reply(buffer + 1 + sizeof(Rep), bytes_read - 1 - sizeof(Rep));
                    std::cout << "✅ Received response: " << reply << "\n";
                    break; // Exit loop if a response is received
                } else {
                    std::cerr << "⚠️ Timeout waiting for server response. Retrying...\n";
                }

            }
        }

        // Wait for response
        ssize_t bytes_read = recvfrom(sock_fd, buffer, sizeof(buffer), 0,
                                      (struct sockaddr*)&server_addr, &server_len);
        if (bytes_read > 0) {
            received_response = true;
            buffer[bytes_read] = '\0';
            std::string reply(buffer + 1 + sizeof(Rep), bytes_read - 1 - sizeof(Rep));
            std::cout << "✅ Received response: " << reply << "\n";
            break; // Exit loop if a response is received
        } else {
            std::cerr << "⚠️ Timeout waiting for server response. Retrying...\n";
        }
    }

    if (!received_response) {
        std::cerr << "❌ No response from server after " << max_retries << " attempts. Giving up.\n";
    }
}

int main(int argc, char** argv) {
    set_epoch();
    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if (argc !=  3) {
        std::cerr << "Usage: " << argv[0] << "<SERVER_IP> <SERVER_PORT>\n";
        return 1;
    }
    char const* SERVER_IP = argv[1];
    int SERVER_PORT = std::stoi(argv[2]);
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    std::string input;
    while (true) {
        std::cout << "\nEnter request (facility_name%params) or 'exit' to quit:\n";
        std::getline(std::cin, input);

        if (input == "exit") {
            std::cout << "Exiting client...\n";
            break;
        }

        std::cout << "Select operation:\n"
                  << "1 - Query Availability\n"
                  << "2 - Book Facility\n"
                  << "3 - Edit Booking\n"
                  << "4 - Monitor Facility\n"
                  << "5 - Reset Availability (Idempotent)\n"
                  << "6 - Swap Bookings (Non-Idempotent)\n"
                  << "Enter operation number: ";

        int op_code;
        std::cin >> op_code;
        std::cin.ignore(); // Ignore leftover newline

        if (op_code < 1 || op_code > 6) {
            std::cout << "Invalid operation code. Try again.\n";
            continue;
        }

        send_request(sock_fd, server_addr, input, op_code);
    }

    close(sock_fd);
    return 0;
}
