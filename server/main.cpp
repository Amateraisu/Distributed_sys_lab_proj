#include "classes.hpp"


// ALL THE MACROS
#define MAX_EVENTS 64
#define LISTEN_PORT 8080
#define BUF_SIZE 1024
#define MAX_PACKET_SIZE 1024

std::set<std::string> const Facilities = {"LT1", "LT2", "LT3", "LT4", "LT5", "LT6", "TR1", "TR2", "TR3", "TR4", "TR5", "TR6"};
bool little_endian;
char const* success_message = "Success\n";
char const* error_message = "Woops, invalid input\n";
std::unordered_map<std::string, std::vector<MonitorClient>> monitor_list;
int udp_fd_global;

static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

std::vector<std::string> splitString(const std::string& str, char delimiter) {
    std::vector<std::string> tokens;
    std::stringstream ss(str); // Use a stringstream
    std::string token;

    while (std::getline(ss, token, delimiter)) { // Read until the delimiter
        tokens.push_back(token);
    }

    return tokens;
}



Day char_to_day(char c) {
    switch (c) {
        case 'M': return Day::Mon;
        case 'T': return Day::Tue;
        case 'W': return Day::Wed;
        case 'H': return Day::Thu;
        case 'F': return Day::Fri;
        case 'S': return Day::Sat;
        case 'U': return Day::Sun;
        default:
            throw std::invalid_argument("Invalid day character: " + std::string(1, c));
    }
}

bool check_endian(unsigned char* buffer) {
    return buffer[0] == '1';
}

bool check_local_endian() {
    int n = 1;
    return *(char*)&n == 1;
}

void cleanup_monitor_list() {
    auto now = time(NULL);
    for (auto it = monitor_list.begin(); it != monitor_list.end(); ++it) {
        std::vector<MonitorClient>& clients = it->second;
        clients.erase(std::remove_if(clients.begin(), clients.end(), 
                      [now](MonitorClient& c) { return c.expiry_time <= now; }),
                      clients.end());
    }
}

void notify_clients(std::string const& facility) {
    std::cout << "Trying to notify_clients\n";
    auto now = time(NULL);
    auto it = monitor_list.find(facility);
    if (it != monitor_list.end()) {
        std::vector<MonitorClient>& clients = it->second;
        for (auto client = clients.begin(); client != clients.end(); ++client) {
            if (client->expiry_time > now) {
                std::cout << "Sending updates\n";
                std::string update_msg = "Facility " + facility + " availability changed!";
                sendto(udp_fd_global, update_msg.c_str(), update_msg.size(), 0, 
                       (struct sockaddr*)&(client->client_addr), sizeof(client->client_addr));
            } else {
                std::cout << "Expired!\n";
            }
        }
    }
    cleanup_monitor_list();

}

// query avail
std::optional<std::string> process_query_availabilities(char* payload, std::map<FacilityDay, std::array<unsigned int, 1440>>& Availabilities){
    std::string res;
    std::string payload_str = payload;
    std::vector<std::string>tokens = splitString(payload_str, '%');
    int n = tokens.size();
    std::string facility = tokens[0];
    if (Facilities.find(facility) == Facilities.end()) {
        return {};
    }
    for (int i = 1; i < n; ++i) {
        auto& t = tokens[i]; // this is the string for the day
        res += "{ ";
        res += t;   
        res += ":";
        // then for every interval, 
        Day d = char_to_day(t[0]);
        // just push back any open range interval;
        auto& arr = Availabilities[{facility, d}];
        // for (auto& [a, b] : Availabilities[{facility, d}]) {
        //     std::string open = std::to_string(a);
        //     std::string close = std::to_string(b);
        //     res += "[" + open + "," + close + "] "; 
        // }
        std::vector<std::pair<ll,ll>>A;
        ll start = -1; 
        for (int i = 0; i < 1440; ++i) {
            if (arr[i] == 0) {
                if (i == 0 || (i > 0 && arr[i - 1] == 1)) {
                    start = i;
                }
            } else {
                if (start != -1 && i - 1 >= 0) {
                    A.emplace_back(start, i - 1);
                }
                start = -1;
            }
        }
        if (start != -1) {
            A.emplace_back(start, 1439);
        }
        for (auto& [a, b] : A) {
            std::string open = std::to_string(a);
            std::string close = std::to_string(b);
            res += "[" + open + "," + close + "] "; 
        }

        res += " }";
        if (i != n - 1) res += ",";
    }
    std::cout << "DEBUG res" << res << '\n';
    return res;
    
    // if error, return "";


}

// try book
bool try_book(Req* ptr, char* payload, std::map<FacilityDay, std::array<unsigned int, 1440>>&  Availabilities, std::map<ll, BookingDetail>& mp) {
    std::string payload_str = payload;
    std::cout << "PAYLOAD IS " << payload_str << '\n';
    std::vector<std::string>tokens = splitString(payload_str, '%');

    if (tokens.size() != 4) return false;
    // characters.
    std::string facility_name = tokens[0];
    Day d = char_to_day(tokens[1].c_str()[0]);
    ll start = stoll(tokens[2]);
    ll end = stoll(tokens[3]);
    if (start >= end) {
        std::cout << "! error\n";
        return false;
    }
    end--;
    for (unsigned int i = start; i <= end; ++i) {
        if (Availabilities[{facility_name, d}][i] == 1) {
            std::cout << "@ error\n";
            return false;
        }
    }
    for (unsigned int i = start; i <= end; ++i) {
        Availabilities[{facility_name, d}][i] = 1;
    }
    mp[ptr->request_id] = BookingDetail{start, end, std::make_pair(facility_name, d)};
    std::cout << "MADE RECORD " << ptr->request_id << ' ' <<start << ' ' << end << '\n';
    notify_clients(facility_name);

    return true;
}

bool try_edit_booking(char* payload, std::map<FacilityDay, std::array<unsigned int, 1440>>&  Availabilities, std::map<ll, BookingDetail>& Bookings) {
    // it should only have an offset.]
    std::cout << "TEST1\n";
    std::string payload_str = payload;

    std::vector<std::string>tokens = splitString(payload_str, '%');
    uint64_t request_id = stoll(tokens[0]);
    ll offset = stoll(tokens[1]);
    ll current_start = Bookings[request_id].book_start;
    ll current_end = Bookings[request_id].book_end;
    FacilityDay key = Bookings[request_id].key;
    std::cout << "TEST2\n";

    std::cout << "Offset is " << stoll(payload_str) << '\n';
    ll new_start = current_start + offset;
    ll new_end = current_end + offset;
    std::cout << current_end << ' ' << offset << ' ' << new_end << '\n';
    if (new_end >= 1440) {
        std::cout << "FAILED1\n";
        return false; // invalid time already
    }
    for (ll i = new_start ; i <= new_end; ++i) {
        if (Availabilities[key][i] <= current_end) continue;
        if (Availabilities[key][i] == 1) {
            std::cout << "FAILED HERE\n";
            return false;
        }
    }

    for (ll i = current_start; i <= current_end ; ++i) Availabilities[key][i] = 0;
    for (ll i = new_start; i <= new_end; ++i) Availabilities[key][i] = 1;
    Bookings[request_id] = BookingDetail{new_start, new_end, key};
    notify_clients(key.first);
    return true;
    

}


bool reset_availability(std::string facility, Day day, 
                        std::map<FacilityDay, std::array<unsigned int, 1440>>& Availabilities,
                        std::map<ll, BookingDetail>& Bookings) {
    if (Facilities.find(facility) == Facilities.end()) return false;

    std::fill(Availabilities[{facility, day}].begin(), Availabilities[{facility, day}].end(), 0);

    // Remove all bookings for that day
    for (auto it = Bookings.begin(); it != Bookings.end(); ) {
        if (it->second.key == FacilityDay{facility, day}) {
            it = Bookings.erase(it);
        } else {
            ++it;
        }
    }
    return true;
}

bool swap_bookings(ll booking_id1, ll booking_id2, 
                   std::map<ll, BookingDetail>& Bookings,
                   std::map<FacilityDay, std::array<unsigned int, 1440>>& Availabilities) {
    if (Bookings.find(booking_id1) == Bookings.end() || Bookings.find(booking_id2) == Bookings.end()) {
        return false;
    }

    BookingDetail& b1 = Bookings[booking_id1];
    BookingDetail& b2 = Bookings[booking_id2];

    if (b1.key != b2.key) return false;  // Must be the same facility & day

    // Clear old times
    for (ll i = b1.book_start; i <= b1.book_end; ++i) Availabilities[b1.key][i] = 0;
    for (ll i = b2.book_start; i <= b2.book_end; ++i) Availabilities[b2.key][i] = 0;

    // Swap times
    std::swap(b1.book_start, b2.book_start);
    std::swap(b1.book_end, b2.book_end);

    // Set new times
    for (ll i = b1.book_start; i <= b1.book_end; ++i) Availabilities[b1.key][i] = 1;
    for (ll i = b2.book_start; i <= b2.book_end; ++i) Availabilities[b2.key][i] = 1;

    return true;
}







int main(int argc, char** argv) {
    // define local data to store availabilities
    little_endian = check_local_endian();
    std::map<FacilityDay, std::array<unsigned int, 1440>> Availabilities;
    std::map<ll, BookingDetail>Bookings;
    
    std::map<uint64_t, bool>request_history;

    int udp_fd, epoll_fd;
    struct sockaddr_in addr;
    struct epoll_event ev, events[MAX_EVENTS];
    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
    udp_fd_global = udp_fd;
    if (udp_fd < 0) {
        perror("Socket error");
        exit(EXIT_FAILURE);
    }
    int optval = 1;
    setsockopt(udp_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET; // IPV4
    addr.sin_addr.s_addr = INADDR_ANY; // allows any incoming connection
    addr.sin_port = htons(LISTEN_PORT); // convert to network byte order

    if (bind(udp_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Bind");
        close(udp_fd);
        exit(EXIT_FAILURE);
    }

    if (set_nonblock(udp_fd) < 0) {
        perror("set_nonblock");
        close(udp_fd);
        exit(EXIT_FAILURE);
    }



    while (1) {
        unsigned char buffer[MAX_PACKET_SIZE];
        unsigned char reply_buffer[MAX_PACKET_SIZE];
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        // datagram, guaranteed to receive or no receive.
        ssize_t bytes_read = recvfrom(udp_fd, buffer, sizeof(buffer), 0, (struct sockaddr*)&client_addr, &client_len);

        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
        } else if (bytes_read > 0) {
            // receive client data
            if (bytes_read < MAX_PACKET_SIZE) {
                buffer[bytes_read] = '\0';
            } else {
                buffer[MAX_PACKET_SIZE - 1] = '\0';
            }

            // Use a random number generator with a uniform distribution
            std::random_device rd;
            std::mt19937 gen(rd());  // Seed RNG
            std::uniform_int_distribution<int> dis(0, 1);

            if (dis(gen) == 0) {  // 50% chance
                std::cout << "Simulated packet loss! Ignoring request.\n";
                continue;
            }

            bool is_little_endian = check_endian(buffer);
            struct Req* request_header = (struct Req*)(buffer + 1);
            uint64_t request_id = request_header->request_id;
            if (request_history.find(request_id) != request_history.end()) continue; // at-most once semantics;
            request_history[request_id] = 1;
            uint32_t op_code = request_header->op_code;
            uint64_t payload_len = request_header->payload_len;
            char* payload = (char*)request_header + sizeof(struct Req);
            printf("Recevied data\n");
            std::cout << "Endian checks local remote " << is_little_endian << ' ' << check_endian(buffer) << '\n';
            if (is_little_endian != check_endian(buffer)) {
                std::cout << "Not the same data, swap\n";
                request_id = __builtin_bswap64(request_id);
                op_code = __builtin_bswap32(op_code);
                payload_len = __builtin_bswap64(payload_len);
            }
            if (is_little_endian) reply_buffer[0] = '1';
            std::cout << request_id << ' ' << op_code << ' ' << payload_len << '\n';
            printf("Received from %s: %d -> Request ID: %lld with message: %s\n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), request_id, payload);
            

            // all the data should be taken care of now already. 
            // the rest of the data is in character bytes, no need to worry about endianness.
            if (op_code == 1) {
                // query availabilities;
                struct Rep* reply_header = (struct Rep*)(reply_buffer + 1);
                std::optional<std::string> reply_data = process_query_availabilities(payload, Availabilities);
                
                reply_header->request_id = request_id;
                if (reply_data.has_value()) {
                    reply_header->status_code = 0;
                    unsigned char* payload = (reply_buffer + 1 + sizeof(Rep));
                    std::size_t payload_len = strlen(reply_data.value().c_str());
                    memcpy(payload, reply_data.value().c_str(), payload_len);
                    reply_header->payload_len = payload_len;
                } else {
                    reply_header->status_code = 1;
                    reply_header->payload_len = 0;
                }
                reply_header->request_id = request_header->request_id;
                uint64_t total_len = sizeof(Rep) + reply_header->payload_len + 1;
                ssize_t sent = sendto(udp_fd, reply_buffer, total_len, 0, (struct sockaddr*)&client_addr, client_len);   
 
            } else if (op_code == 2) {
                // book.
                // requires: facility name, day, hour, [time_start, time_end);
                // to account for this, just make time_end -= 1
                // check if the range [time_start ~ time_end - 1] is available;

                // if it is , book it.
                // else no.
                // check avail.
                std::cout << "OPCODE 2 !\n";
                unsigned char* payload_inner = (reply_buffer + 1 + sizeof(Rep));
                struct Rep* reply_header = (struct Rep*)(reply_buffer + 1);
                bool can_book = try_book(request_header, (char*)payload, Availabilities, Bookings);
                std::size_t payload_len;
                if (can_book) {
                    reply_header->status_code = 1;
                    payload_len = strlen(success_message);
                    memcpy(payload_inner, success_message, strlen(success_message));

                } else {
                    reply_header->status_code = 0;
                    payload_len = strlen(error_message);
                    memcpy(payload_inner, error_message, strlen(error_message));
                }
                reply_header->request_id = request_header->request_id;
                reply_header->payload_len = payload_len;
                uint64_t total_len = sizeof(Rep) + reply_header->payload_len + 1;
                ssize_t sent = sendto(udp_fd, reply_buffer, total_len, 0, (struct sockaddr*)&client_addr, client_len);
                
                // at this point, already booked.
            


                // send updates to those that are watching this.
            } else if (op_code == 3) {
                // change
                // cancel first, then edit the booking.
                // check for offset in minutes.
                bool can_edit_booking = try_edit_booking((char*)payload, Availabilities, Bookings);
                std::cout << "OPCODE 3 !\n";
                unsigned char* payload_inner = (reply_buffer + 1 + sizeof(Rep));
                struct Rep* reply_header = (struct Rep*)(reply_buffer + 1);
                std::size_t payload_len;
                if (can_edit_booking) {
                    reply_header->status_code = 1;
                    payload_len = strlen(success_message);
                    memcpy(payload_inner, success_message, strlen(success_message));
                    std::cout << "SUICCESS\n";

                } else {
                    reply_header->status_code = 0;
                    payload_len = strlen(error_message);
                    memcpy(payload_inner, error_message, strlen(error_message));
                    std::cout << "FAILED\n";
                }
                reply_header->request_id = request_header->request_id;
                reply_header->payload_len = payload_len;
                uint64_t total_len = sizeof(Rep) + reply_header->payload_len + 1;
                ssize_t sent = sendto(udp_fd, reply_buffer, total_len, 0, (struct sockaddr*)&client_addr, client_len);
            } else if (op_code == 4) {
                // monitor
                // who is monitoring what then ? 
                std::cout << "OPCODE 4 (Monitoring Request)!\n";
                
                std::string payload_str = payload;
                std::vector<std::string> tokens = splitString(payload_str, '%');
                if (tokens.size() != 2) {
                    std::cout << "Invalid monitoring request\n";
                    continue;
                }

                std::string facility_name = tokens[0];
                int monitor_duration = std::stoi(tokens[1]);

                if (Facilities.find(facility_name) == Facilities.end()) {
                    std::cout << "Facility not found\n";
                    continue;
                }

                // Add client to monitor list
                time_t expiry_time = time(NULL) + monitor_duration * 60;
                monitor_list[facility_name].push_back({client_addr, expiry_time});

                std::cout << "Client registered for monitoring " << facility_name 
                        << " for " << monitor_duration << " seconds\n";
            } else if (op_code == 5) {
                // idempotent operation
                    std::string payload_str = payload;
                    std::vector<std::string> tokens = splitString(payload_str, '%');
                    if (tokens.size() != 2) continue;

                    std::string facility_name = tokens[0];
                    Day d = char_to_day(tokens[1][0]);

                    struct Rep* reply_header = (struct Rep*)(reply_buffer + 1);
                    bool success = reset_availability(facility_name, d, Availabilities, Bookings);
                    std::size_t payload_len;

                    if (success) {
                        reply_header->status_code = 1;
                        payload_len = strlen(success_message);
                        memcpy(reply_buffer + 1 + sizeof(Rep), success_message, payload_len);
                    } else {
                        reply_header->status_code = 0;
                        payload_len = strlen(error_message);
                        memcpy(reply_buffer + 1 + sizeof(Rep), error_message, payload_len);
                    }

                    reply_header->request_id = request_header->request_id;
                    reply_header->payload_len = payload_len;
                    sendto(udp_fd, reply_buffer, sizeof(Rep) + payload_len + 1, 0, 
                        (struct sockaddr*)&client_addr, client_len);
            } else if (op_code == 6) {
                // non-idempotent operation.
                    std::string payload_str = payload;
                    std::vector<std::string> tokens = splitString(payload_str, '%');
                    if (tokens.size() != 2) continue;

                    ll booking_id1 = stoll(tokens[0]);
                    ll booking_id2 = stoll(tokens[1]);

                    struct Rep* reply_header = (struct Rep*)(reply_buffer + 1);
                    bool success = swap_bookings(booking_id1, booking_id2, Bookings, Availabilities);
                    std::size_t payload_len;

                    if (success) {
                        reply_header->status_code = 1;
                        payload_len = strlen(success_message);
                        memcpy(reply_buffer + 1 + sizeof(Rep), success_message, payload_len);
                    } else {
                        reply_header->status_code = 0;
                        payload_len = strlen(error_message);
                        memcpy(reply_buffer + 1 + sizeof(Rep), error_message, payload_len);
                    }

                    reply_header->request_id = request_header->request_id;
                    reply_header->payload_len = payload_len;
                    sendto(udp_fd, reply_buffer, sizeof(Rep) + payload_len + 1, 0, 
                        (struct sockaddr*)&client_addr, client_len);
            } else {
                // std::unreachable()
            }
            // const char* reply = "This is just a reply";
            // ssize_t sent = sendto(udp_fd, reply, 20, 0, (struct sockaddr*)&client_addr, client_len);
            // if (sent < 0) {
            //     perror("Sendto");
            // }

        }
    }
    close(udp_fd);

    return 0;
}
