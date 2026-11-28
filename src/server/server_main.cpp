/**
 * @file server_main.cpp
 * @brief Server process: start-up, poll()-based main loop, graceful shutdown.
 *
 * Usage: ./server [port] [db_path] [users_file]
 *        defaults: 8080  data/server.db  config/users.txt
 *
 * Main thread
 *   poll() multiplexes three descriptors:
 *     - the listening socket      -> accept() a client, spawn its worker thread
 *     - stdin                     -> server console commands (shutdown, users, help)
 *     - the read end of a pipe    -> written by the SIGINT/SIGTERM handler
 *                                    ("self-pipe trick": the handler only does an
 *                                     async-signal-safe write(); all real work
 *                                     happens here in normal thread context)
 * Shutdown (Ctrl+C, SIGTERM, or typing "shutdown"):
 *   1. stop accepting, set running = false
 *   2. broadcast "Server shutting down..." to every online client
 *   3. shutdown() every socket so blocked workers return from recv()
 *   4. join all worker threads, close the database
 */
#include <fcntl.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

#include "client_handler.h"
#include "connection_tracker.h"
#include "console.h"
#include "db.h"
#include "network.h"
#include "protocol.h"
#include "server_context.h"
#include "session_manager.h"
#include "user_registry.h"

namespace {

int g_signal_pipe[2] = {-1, -1};

extern "C" void on_signal(int signo) {
    const int saved = errno;
    const char c = static_cast<char>(signo);
    ssize_t ignored = ::write(g_signal_pipe[1], &c, 1);  // async-signal-safe
    (void)ignored;
    errno = saved;
}

bool install_signal_handlers() {
    if (::pipe(g_signal_pipe) < 0) return false;
    ::fcntl(g_signal_pipe[1], F_SETFL, O_NONBLOCK);

    struct sigaction sa{};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
    std::signal(SIGPIPE, SIG_IGN);  // dead peers -> EPIPE instead of killing the server
    return true;
}

void print_console_help() {
    chat::log_event("Server console commands: users | shutdown | help   (Ctrl+C also shuts down)");
}

/** Handles one line typed on the server console. Returns false to shut down. */
bool handle_console_line(const std::string &line, const chat::SessionManager &sessions) {
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    if (cmd.empty()) return true;
    if (cmd == "shutdown" || cmd == "quit" || cmd == "exit") return false;
    if (cmd == "users") {
        auto users = sessions.online_users();
        std::string list;
        for (const auto &u : users) list += " " + u;
        chat::log_event("Online users (" + std::to_string(users.size()) + "):" + list);
    } else {
        print_console_help();
    }
    return true;
}

void broadcast_shutdown(chat::SessionManager &sessions) {
    const chat::Packet pkt = chat::create_packet(chat::ResponseType::SERVER_SHUTDOWN, "server", "",
                                                 "Server shutting down...");
    auto all = sessions.all();
    for (const auto &s : all) s->send(pkt);
    chat::log_event("Shutdown broadcast sent to " + std::to_string(all.size()) + " client(s)");
}

}  // namespace

int main(int argc, char *argv[]) {
    uint16_t port = chat::DEFAULT_PORT;
    std::string db_path = "data/server.db";
    std::string users_path = "config/users.txt";

    if (argc >= 2) {
        int p = std::atoi(argv[1]);
        if (p <= 0 || p > 65535) {
            std::cerr << "Usage: " << argv[0] << " [port] [db_path] [users_file]\n";
            return 1;
        }
        port = static_cast<uint16_t>(p);
    }
    if (argc >= 3) db_path = argv[2];
    if (argc >= 4) users_path = argv[3];

    if (!install_signal_handlers()) {
        std::cerr << "[server] cannot create signal pipe\n";
        return 1;
    }

    std::cout << "======================================================\n"
              << "   Multi-User Messaging Server\n"
              << "======================================================\n";

    // 1. Valid users (file based)
    chat::UserRegistry users;
    std::string err;
    if (!users.load_from_file(users_path, err)) {
        std::cerr << "[server] Fatal: " << err << "\n";
        return 1;
    }

    // 2. Persistent storage
    if (!chat::db_init(db_path)) {
        std::cerr << "[server] Fatal: cannot initialise database " << db_path << "\n";
        return 1;
    }
    for (const auto &u : users.all()) chat::db_add_user(u);
    chat::log_event("Loaded " + std::to_string(users.size()) + " valid users from " + users_path);
    chat::log_event("Database ready: " + db_path);

    // 3. Listening socket
    int listen_fd = chat::create_listen_socket(port, err);
    if (listen_fd < 0) {
        std::cerr << "[server] Fatal: " << err << "\n";
        chat::db_close();
        return 1;
    }
    chat::log_event("Listening on port " + std::to_string(port));
    print_console_help();

    chat::SessionManager sessions;
    chat::ConnectionTracker connections;
    std::atomic<bool> running{true};
    chat::ServerContext ctx{&sessions, &users, &connections, &running, 64};

    // 4. Event loop
    bool stdin_open = true;
    std::string console_buf;
    while (running.load()) {
        pollfd fds[3] = {
            {listen_fd, POLLIN, 0},
            {g_signal_pipe[0], POLLIN, 0},
            {stdin_open ? STDIN_FILENO : -1, POLLIN, 0},  // fd -1 is ignored by poll
        };
        int ready = ::poll(fds, 3, 1000);  // 1 s tick: reap finished workers
        connections.reap_finished();
        if (ready < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[server] poll(): " << std::strerror(errno) << "\n";
            break;
        }

        if (fds[1].revents & POLLIN) {
            char sig = 0;
            if (::read(g_signal_pipe[0], &sig, 1) > 0) {
                chat::log_event(std::string("Received ") + (sig == SIGTERM ? "SIGTERM" : "SIGINT (Ctrl+C)"));
            }
            running.store(false);
            break;
        }

        if (fds[2].revents & (POLLIN | POLLHUP)) {
            char buf[256];
            ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) {
                stdin_open = false;  // e.g. started in background; Ctrl+C / SIGTERM still work
            } else {
                console_buf.append(buf, static_cast<size_t>(n));
                size_t nl;
                while (running.load() && (nl = console_buf.find('\n')) != std::string::npos) {
                    std::string line = console_buf.substr(0, nl);
                    console_buf.erase(0, nl + 1);
                    if (!handle_console_line(line, sessions)) {
                        chat::log_event("Shutdown requested from server console");
                        running.store(false);
                    }
                }
            }
        }

        if (running.load() && (fds[0].revents & POLLIN)) {
            sockaddr_in addr{};
            socklen_t len = sizeof(addr);
            int client_fd = ::accept(listen_fd, reinterpret_cast<sockaddr *>(&addr), &len);
            if (client_fd < 0) {
                if (errno != EINTR) chat::log_event(std::string("accept(): ") + std::strerror(errno));
                continue;
            }
            char ip[INET_ADDRSTRLEN] = {0};
            ::inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
            std::string peer = std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
            chat::log_event("Incoming connection from " + peer);

            connections.add_fd(client_fd);
            auto done = std::make_shared<std::atomic<bool>>(false);
            std::thread worker([client_fd, peer, ctx, done]() {
                chat::handle_client(client_fd, peer, ctx);
                done->store(true);
            });
            connections.start_worker(std::move(worker), done);
        }
    }

    // 5. Graceful shutdown
    chat::log_event("Server shutting down...");
    ::close(listen_fd);
    broadcast_shutdown(sessions);
    connections.shutdown_all();
    connections.join_all();
    chat::db_close();
    chat::log_event("All client threads joined, database closed. Bye.");
    return 0;
}
