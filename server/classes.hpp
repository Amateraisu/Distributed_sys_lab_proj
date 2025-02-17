#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <map>
#include <string>
#include <set>
#include <optional>
#include <cstdlib>
#include <sstream>
#include <vector>
#include <iostream>
#include <cstring> 
#include <cstdint> 
#include <netinet/in.h> 
#include <chrono>
#include <unordered_map>
#include <array>
#include <algorithm>

using ll = long long;
enum Day {
    Mon = 1,
    Tue = 2,
    Wed = 3,
    Thu = 4,
    Fri = 5,
    Sat = 6,
    Sun = 7
};
typedef std::pair<std::string, Day> FacilityDay; 
typedef std::pair<ll, ll> AvailInterval;

// total of 24 bytes.
// start of the buffer should have a bit that is either little or big endian flag.
struct Req {
    uint64_t request_id;
    uint64_t payload_len;
    uint32_t op_code;
};

struct Rep {
    uint64_t request_id;
    uint64_t payload_len;
    uint32_t status_code;
};

struct BookingDetail {
    ll book_start;
    ll book_end;
    FacilityDay key;
};

struct MonitorClient {
    struct sockaddr_in client_addr;  // Client address
    time_t expiry_time;  // Time at which monitoring expires
};
