#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
#endif

#include "protocol.h"
#include "db.h"
#include "session_manager.h"
#include "client_handler.h"

namespace {

std::atomic<bool> g_running{true};
int g_listen_fd = -1;

void sigint_handler(int /*signum*/) {
    g_running.store(false);
    if (g_listen_fd >= 0) {
#ifdef _WIN32
        closesocket(g_listen_fd);
#else
        ::shutdown(g_listen_fd, SHUT_RDWR);
        ::close(g_listen_fd);
#endif
        g_listen_fd = -1;
    }
}

}  // namespace

int main(int argc, char *argv[]) {
    uint16_t port = chat::DEFAULT_PORT;
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

#ifdef _WIN32
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#else
    std::signal(SIGPIPE, SIG_IGN);

    struct sigaction sa{};
    sa.sa_handler = sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGINT, &sa, nullptr);
#endif

    std::cout << "======================================================\n";
    std::cout << "   Multi-User Messaging Server (Linux C++17)           \n";
    std::cout << "======================================================\n";

    // 1. Initialize Persistent SQLite Database
    if (!chat::db_init(db_path)) {
        std::cerr << "[server] Fatal: Failed to initialize SQLite database at " << db_path << "\n";
        return 1;
    }
    std::cout << "[server] Database initialized successfully (" << db_path << ")\n";

    // 2. Initialize In-Memory Session Manager
    chat::SessionManager sessions;

    // 3. Create TCP Listening Socket
    g_listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (g_listen_fd < 0) {
        std::cerr << "[server] Fatal: socket() creation failed: " << std::strerror(errno) << "\n";
        chat::db_close();
        return 1;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
    ::setsockopt(g_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(g_listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[server] Fatal: bind() failed on port " << port << ": " << std::strerror(errno) << "\n";
        if (g_listen_fd >= 0) {
#ifdef _WIN32
            closesocket(g_listen_fd);
#else
            ::close(g_listen_fd);
#endif
            g_listen_fd = -1;
        }
        chat::db_close();
        return 1;
    }

    // Support at least 5 concurrent clients (SOMAXCONN allows backlog of connections)
    if (::listen(g_listen_fd, SOMAXCONN) < 0) {
        std::cerr << "[server] Fatal: listen() failed: " << std::strerror(errno) << "\n";
        if (g_listen_fd >= 0) {
#ifdef _WIN32
            closesocket(g_listen_fd);
#else
            ::close(g_listen_fd);
#endif
            g_listen_fd = -1;
        }
        chat::db_close();
        return 1;
    }

    std::cout << "[server] Listening on port " << port << ". Ready for connections.\n";
    std::cout << "[server] (Press Ctrl+C to shut down server gracefully)\n";

    // 4. Client Connection Accept Loop
    while (g_running.load()) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(g_listen_fd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

        if (client_fd < 0) {
            if (!g_running.load() || errno == EINTR || errno == EBADF || errno == EINVAL) {
                break;
            }
            std::cerr << "[server] accept() failed: " << std::strerror(errno) << "\n";
            continue;
        }

        char ip_buf[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
        std::cout << "[server] Incoming connection from " << ip_buf << ":"
                  << ntohs(client_addr.sin_port) << " (fd=" << client_fd << ")\n";

        // Dispatch a detached worker thread for concurrent client handling
        chat::ClientHandlerContext ctx{client_fd, &sessions, &g_running};
        std::thread worker(chat::handle_client, ctx);
        worker.detach();
    }

    // 5. Graceful Server Shutdown: Broadcast notification to all active clients
    std::cout << "\n[server] Shutting down... broadcasting notification to active clients.\n";

    auto active_sessions = sessions.get_all_sessions();
    chat::Packet shutdown_pkt = chat::create_packet(
        static_cast<uint16_t>(chat::ResponseType::SERVER_SHUTDOWN),
        "server",
        "",
        "Server shutting down..."
    );

    for (const auto &entry : active_sessions) {
        int fd = entry.second;
        if (fd >= 0) {
            chat::send_packet(fd, shutdown_pkt);
#ifdef _WIN32
            closesocket(fd);
#else
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
#endif
        }
    }

    // Flush and close SQLite cleanly
    chat::db_close();

#ifdef _WIN32
    WSACleanup();
#endif

    std::cout << "[server] Clean shutdown completed successfully.\n";
    return 0;
}
