#include "protocol.h"

#include <cstring>
#include <algorithm>
#include <cerrno>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netinet/in.h>
#endif

namespace chat {

void Packet::set_sender(const std::string &name) {
    std::memset(sender, 0, MAX_USERNAME_LEN);
    size_t len = std::min(name.size(), MAX_USERNAME_LEN - 1);
    std::memcpy(sender, name.data(), len);
    sender[len] = '\0';
}

void Packet::set_recipient(const std::string &name) {
    std::memset(recipient, 0, MAX_USERNAME_LEN);
    size_t len = std::min(name.size(), MAX_USERNAME_LEN - 1);
    std::memcpy(recipient, name.data(), len);
    recipient[len] = '\0';
}

void Packet::set_payload(const std::string &text) {
    std::memset(payload, 0, MAX_PAYLOAD_LEN);
    size_t len = std::min(text.size(), MAX_PAYLOAD_LEN - 1);
    std::memcpy(payload, text.data(), len);
    payload[len] = '\0';
    payload_len = static_cast<uint32_t>(len);
}

std::string Packet::get_sender() const {
    return std::string(sender);
}

std::string Packet::get_recipient() const {
    return std::string(recipient);
}

std::string Packet::get_payload() const {
    return std::string(payload);
}

bool send_packet(int socket_fd, const Packet &pkt) {
    Packet net_pkt = pkt;

    // Convert multi-byte integers to Network Byte Order (Big-Endian)
    net_pkt.type = htons(pkt.type);
    net_pkt.status = htons(pkt.status);
    net_pkt.payload_len = htonl(pkt.payload_len);

    const char *buffer = reinterpret_cast<const char *>(&net_pkt);
    size_t total_sent = 0;
    size_t to_send = sizeof(Packet);

    while (total_sent < to_send) {
#ifdef _WIN32
        int bytes = send(socket_fd, buffer + total_sent, static_cast<int>(to_send - total_sent), 0);
#else
        ssize_t bytes = send(socket_fd, buffer + total_sent, to_send - total_sent, MSG_NOSIGNAL);
#endif
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (bytes == 0) {
            return false;
        }
        total_sent += static_cast<size_t>(bytes);
    }
    return true;
}

bool recv_packet(int socket_fd, Packet &pkt) {
    char *buffer = reinterpret_cast<char *>(&pkt);
    size_t total_received = 0;
    size_t to_receive = sizeof(Packet);

    while (total_received < to_receive) {
#ifdef _WIN32
        int bytes = recv(socket_fd, buffer + total_received, static_cast<int>(to_receive - total_received), 0);
#else
        ssize_t bytes = recv(socket_fd, buffer + total_received, to_receive - total_received, 0);
#endif
        if (bytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (bytes == 0) {
            return false; // Disconnected or EOF
        }
        total_received += static_cast<size_t>(bytes);
    }

    // Convert multi-byte integers back to Host Byte Order
    pkt.type = ntohs(pkt.type);
    pkt.status = ntohs(pkt.status);
    pkt.payload_len = ntohl(pkt.payload_len);

    // Ensure null-termination safety on string buffers
    pkt.sender[MAX_USERNAME_LEN - 1] = '\0';
    pkt.recipient[MAX_USERNAME_LEN - 1] = '\0';
    pkt.payload[MAX_PAYLOAD_LEN - 1] = '\0';

    return true;
}

Packet create_packet(uint16_t type,
                     const std::string &sender,
                     const std::string &recipient,
                     const std::string &payload,
                     StatusCode status) {
    Packet pkt;
    pkt.type = type;
    pkt.status = static_cast<uint16_t>(status);
    pkt.set_sender(sender);
    pkt.set_recipient(recipient);
    pkt.set_payload(payload);
    return pkt;
}

Packet create_error_packet(StatusCode code, const std::string &error_msg) {
    Packet pkt;
    pkt.type = static_cast<uint16_t>(ResponseType::RESP_ERROR);
    pkt.status = static_cast<uint16_t>(code);
    pkt.set_payload(error_msg);
    return pkt;
}

}  // namespace chat
