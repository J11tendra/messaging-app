#include "receiver_thread.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include "protocol.hpp"

namespace chat {

namespace {

// Reads an exact `len`-byte frame from the TCP socket.
// Returns: 1 on full packet, 0 on clean EOF/disconnect, -1 on error.
int recv_exact_pkt(int fd, void* buf, size_t len) {
    auto* ptr = static_cast<uint8_t*>(buf);
    size_t total_recv = 0;

    while (total_recv < len) {
        ssize_t n = ::recv(fd, ptr + total_recv, len - total_recv, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return (total_recv == 0) ? 0 : -1;
        }
        total_recv += static_cast<size_t>(n);
    }
    return 1;
}

}  // namespace

void run_receiver_thread(ReceiverContext ctx) {
    const int sock_fd = ctx.sock_fd;
    std::atomic<bool>* running = ctx.running;
    std::atomic<bool>* connected = ctx.connected;

    while ((running == nullptr || running->load()) &&
           (connected == nullptr || connected->load())) {
        Packet pkt{};
        int rc = recv_exact_pkt(sock_fd, &pkt, PACKET_SIZE_BYTES);
        if (rc == 0) {
            if (connected != nullptr && connected->load()) {
                std::cout << "\n[info] Server closed the connection.\n> " << std::flush;
            }
            break;
        }
        if (rc < 0) {
            if ((running != nullptr && !running->load()) ||
                (connected != nullptr && !connected->load()) ||
                errno == EBADF || errno == EINVAL) {
                break;
            }
            std::cerr << "\n[error] Socket receive error: " << std::strerror(errno)
                      << "\n> " << std::flush;
            break;
        }

        const CommandType cmd = get_command(pkt);
        const StatusCode status = get_status(pkt);
        const std::string sender = get_sender_str(pkt);
        const std::string payload = get_payload_str(pkt);

        if (cmd == CommandType::SERVER_NOTICE || status == StatusCode::SERVER_SHUTTING_DOWN) {
            std::cout << "\n[SERVER ALERT] " << payload << "\n" << std::flush;
            if (connected != nullptr) {
                connected->store(false);
            }
            break;
        }

        if (cmd == CommandType::SEND_MESSAGE && status == StatusCode::OK && sender != "server") {
            // Real-time incoming message from another user or replayed offline message
            if (payload.size() > 1 && payload[0] == '[' && payload.find("] ") != std::string::npos) {
                std::cout << "\n" << payload << "\n> " << std::flush;
            } else {
                std::cout << "\n[" << sender << "]: " << payload << "\n> " << std::flush;
            }
            continue;
        }

        if (status != StatusCode::OK) {
            std::cout << "\n[error] " << payload << "\n> " << std::flush;
        } else if (!payload.empty()) {
            std::cout << "\n" << payload << "\n> " << std::flush;
        }
    }

    if (connected != nullptr) {
        connected->store(false);
    }
}

}  // namespace chat
