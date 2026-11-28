/**
 * @file network.cpp
 * @brief TCP socket helpers and exact-length Packet send/receive.
 */
#include "network.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace chat {

bool send_packet(int socket_fd, const Packet &pkt) {
    if (socket_fd < 0) return false;

    // Host -> network byte order for the multi-byte integer fields.
    Packet wire = pkt;
    wire.type = htons(pkt.type);
    wire.status = htons(pkt.status);
    wire.payload_len = htonl(pkt.payload_len);

    const char *buf = reinterpret_cast<const char *>(&wire);
    size_t sent = 0;
    while (sent < sizeof(Packet)) {
        ssize_t n = ::send(socket_fd, buf + sent, sizeof(Packet) - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool recv_packet(int socket_fd, Packet &pkt) {
    if (socket_fd < 0) return false;

    char *buf = reinterpret_cast<char *>(&pkt);
    size_t got = 0;
    while (got < sizeof(Packet)) {
        ssize_t n = ::recv(socket_fd, buf + got, sizeof(Packet) - got, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;  // 0 = orderly EOF, <0 = error / timeout
        got += static_cast<size_t>(n);
    }

    pkt.type = ntohs(pkt.type);
    pkt.status = ntohs(pkt.status);
    pkt.payload_len = ntohl(pkt.payload_len);

    // Never trust the peer: force termination of all string buffers.
    pkt.sender[MAX_USERNAME_LEN - 1] = '\0';
    pkt.recipient[MAX_USERNAME_LEN - 1] = '\0';
    pkt.payload[MAX_PAYLOAD_LEN - 1] = '\0';
    return pkt.payload_len < MAX_PAYLOAD_LEN;
}

int create_listen_socket(uint16_t port, std::string &err) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        err = std::string("socket(): ") + std::strerror(errno);
        return -1;
    }
    int opt = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        err = "bind() on port " + std::to_string(port) + ": " + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    if (::listen(fd, SOMAXCONN) < 0) {
        err = std::string("listen(): ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    return fd;
}

int connect_to_server(const std::string &ip, uint16_t port, std::string &err) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        err = "invalid server IP address '" + ip + "'";
        return -1;
    }
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        err = std::string("socket(): ") + std::strerror(errno);
        return -1;
    }
    if (::connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        err = "cannot reach " + ip + ":" + std::to_string(port) + " (" + std::strerror(errno) + ")";
        ::close(fd);
        return -1;
    }
    return fd;
}

void set_recv_timeout(int fd, int seconds) {
    timeval tv{};
    tv.tv_sec = seconds;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

void close_socket(int fd) {
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
}

}  // namespace chat
