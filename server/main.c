#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>



#define MAX_EVENTS 64
#define LISTEN_PORT 8080
#define BUF_SIZE 1024


static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main(int argc, char** argv) {
    int udp_fd, epoll_fd;
    struct sockaddr_in addr;
    struct epoll_event ev, events[MAX_EVENTS];
    udp_fd = socket(AF_INET, SOCK_DGRAM, 0);
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

    // create the epoll instance
    epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("Epoll_create1 error");
        close(udp_fd);
        exit(EXIT_FAILURE);
    }

    ev.events |= EPOLLIN;
    ev.data.fd = udp_fd;

 
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udp_fd, &ev) < 0) {
        perror("Epoll_ctl");
        close(epoll_fd);
        close(udp_fd);
        exit(EXIT_FAILURE);
    }

    while (1) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("Epoll_wait");
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if (fd == udp_fd && (events[i].events & EPOLLIN)) {
                char buf[BUF_SIZE];
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);

                // receive data (non_blocking)
                ssize_t bytes_read = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr*)&client_addr, &client_len);

                if (bytes_read < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        continue;
                    }
                } else if (bytes_read > 0) {
                    if (bytes_read < BUF_SIZE) {
                        buf[bytes_read] = '\0';
                    } else {
                        buf[BUF_SIZE - 1] = '\0';
                    }

                    printf("Received from %s: %d -> %s \n", inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), buf);
                    // echo data back 
                    ssize_t sent = sendto(fd, buf, bytes_read, 0, (struct sockaddr*)&client_addr, client_len);
                    if (sent < 0) {
                        perror("Sendto");
                    }

                }

            }
        }
    }

    close(epoll_fd);
    close(udp_fd);

    return 0;
}
