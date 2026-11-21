#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "client_handler.hpp"
#include "database.hpp"
#include "protocol.hpp"
#include "session_manager.hpp"

namespace {

std::atomic<bool> g_running{true};
int g_listen_fd = -1;

void sigint_handler(int /*signum*/) {
    g_running.store(false);
    if (g_listen_fd >= 0) {
        // Unblock accept() in the master listening thread
        ::shutdown(g_listen_fd, SHUT_RDWR);
        ::close(g_listen_fd);
        g_listen_fd = -1;
    }
}

bool send_all_bytes(int fd, const void* buf, size_t len) {
    const auto* ptr = static_cast<const uint8_t*>(buf);
    size_t sent_total = 0;

    while (sent_total < len) {
        ssize_t n = ::send(fd, ptr + sent_total, len - sent_total, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        sent_total += static_cast<size_t>(n);
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    uint16_t port = chat::DEFAULT_SERVER_PORT;
    std::string db_path = "data/server.db";

    if (argc >= 2) {
        int parsed_port = std::stoi(argv[1]);
        if (parsed_port > 0 && parsed_port <= 65535) {
            port = static_cast<uint16_t>(parsed_port);
        }
    }
    if (argc >= 3) {
        db_path = argv[2];
    }

    // Ignore SIGPIPE globally so broken client sockets return EPIPE instead of killing the server
    std::signal(SIGPIPE, SIG_IGN);

    struct sigaction sa{};
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGINT, &sa, nullptr);

    chat::Database db(db_path);
    if (!db.init()) {
        std::cerr << "[server] Failed to initialize SQLite database at " << db_path << "\n";
        return 1;
    }

    chat::SessionManager sessions;

    g_listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        std::cerr << "[server] socket() failed: " << std::strerror(errno) << "\n";
        db.close();
        return 1;
    }

    int opt = 1;
    ::setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(g_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[server] bind() failed on port " << port
                  << ": " << std::strerror(errno) << "\n";
        if (g_listen_fd >= 0) {
            ::close(g_listen_fd);
            g_listen_fd = -1;
        }
        db.close();
        return 1;
    }

    if (::listen(g_listen_fd, SOMAXCONN) < 0) {
        std::cerr << "[server] listen() failed: " << std::strerror(errno) << "\n";
        if (g_listen_fd >= 0) {
            ::close(g_listen_fd);
            g_listen_fd = -1;
        }
        db.close();
        return 1;
    }

    std::cout << "[server] Listening on TCP port " << port
              << " (db: " << db_path << "). Press Ctrl+C to shut down.\n";

    while (g_running.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(g_listen_fd,
                                  reinterpret_cast<sockaddr*>(&client_addr),
                                  &client_len);
        if (client_fd < 0) {
            if (!g_running.load() || errno == EINTR || errno == EBADF || errno == EINVAL) {
                break;
            }
            std::cerr << "[server] accept() failed: " << std::strerror(errno) << "\n";
            continue;
        }

        char ip_buf[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
        std::cout << "[server] Accepted connection from " << ip_buf
                  << ":" << ntohs(client_addr.sin_port)
                  << " (fd=" << client_fd << ")\n";

        chat::ClientHandlerContext ctx{client_fd, &sessions, &db, &g_running};
        std::thread worker(chat::handle_client, ctx);
        worker.detach();
    }

    std::cout << "\n[server] Shutting down... broadcasting notification to active clients.\n";

    const std::vector<std::pair<std::string, int>> active_sessions = sessions.get_all_sessions();
    const chat::Packet notice = chat::make_packet(chat::CommandType::SERVER_NOTICE,
                                                  chat::StatusCode::SERVER_SHUTTING_DOWN,
                                                  "server",
                                                  "",
                                                  "Server shutting down...");

    for (const auto& entry : active_sessions) {
        const int fd = entry.second;
        if (fd >= 0) {
            send_all_bytes(fd, &notice, sizeof(notice));
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
    }

    // Flush and close SQLite cleanly before exiting
    db.close();
    std::cout << "[server] Shutdown complete.\n";
    return 0;
}
