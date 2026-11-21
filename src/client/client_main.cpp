#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "protocol.hpp"
#include "receiver_thread.hpp"

namespace {

std::atomic<bool> g_running{true};
std::atomic<bool> g_connected{false};
int g_client_fd = -1;

void client_sigint_handler(int /*signum*/) {
    g_running.store(false);
    g_connected.store(false);
    if (g_client_fd >= 0) {
        ::shutdown(g_client_fd, SHUT_RDWR);
    }
}

bool send_all_pkt_bytes(int fd, const void* buf, size_t len) {
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

bool send_packet_to_server(int fd, const chat::Packet& pkt) {
    return send_all_pkt_bytes(fd, &pkt, chat::PACKET_SIZE_BYTES);
}

int recv_exact_pkt_bytes(int fd, void* buf, size_t len) {
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

void close_client_socket(int& fd) {
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
        fd = -1;
    }
}

std::string trim_ws(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

void print_help() {
    std::cout << "Available commands:\n"
              << "  connect <server IP> <user>   - Authenticate and join chat (default port 8080)\n"
              << "  sendmessage <user> <msg>     - Send real-time or queued offline message\n"
              << "  getmessages <user>           - Fetch full two-way chat history with <user>\n"
              << "  deletemessages <user>        - Permanently delete two-way history with <user>\n"
              << "  getuserlist                  - List currently connected online users\n"
              << "  shutdown                     - Disconnect gracefully and exit client\n"
              << "  help                         - Show this command list\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    uint16_t default_port = chat::DEFAULT_SERVER_PORT;
    if (argc >= 2) {
        int parsed = std::stoi(argv[1]);
        if (parsed > 0 && parsed <= 65535) {
            default_port = static_cast<uint16_t>(parsed);
        }
    }

    std::signal(SIGPIPE, SIG_IGN);

    struct sigaction sa{};
    sa.sa_handler = client_sigint_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGINT, &sa, nullptr);

    std::string current_user;
    std::thread rx_thread;

    auto join_receiver_if_needed = [&]() {
        if (rx_thread.joinable()) {
            rx_thread.join();
        }
    };

    std::cout << "Multi-User Client-Server Messaging CLI\n";
    print_help();

    std::string line;
    while (g_running.load()) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) {
            break;
        }

        const std::string trimmed = trim_ws(line);
        if (trimmed.empty()) {
            continue;
        }

        std::istringstream iss(trimmed);
        std::string cmd;
        iss >> cmd;

        if (cmd == "help") {
            print_help();
            continue;
        }

        if (cmd == "connect") {
            std::string server_ip;
            std::string username;
            iss >> server_ip >> username;

            if (server_ip.empty() || username.empty()) {
                std::cout << "[usage] connect <server IP> <user>\n";
                continue;
            }

            if (g_connected.load() || g_client_fd >= 0) {
                std::cout << "[info] Already connected as '" << current_user
                          << "'. Run 'shutdown' first to reconnect.\n";
                continue;
            }

            join_receiver_if_needed();

            int sock_fd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (sock_fd < 0) {
                std::cerr << "[error] socket() failed: " << std::strerror(errno) << "\n";
                continue;
            }

            sockaddr_in srv_addr{};
            srv_addr.sin_family = AF_INET;
            srv_addr.sin_port = htons(default_port);
            if (::inet_pton(AF_INET, server_ip.c_str(), &srv_addr.sin_addr) <= 0) {
                std::cerr << "[error] Invalid server IP address: " << server_ip << "\n";
                ::close(sock_fd);
                continue;
            }

            if (::connect(sock_fd, reinterpret_cast<sockaddr*>(&srv_addr), sizeof(srv_addr)) < 0) {
                std::cerr << "[error] connect() to " << server_ip << ":" << default_port
                          << " failed: " << std::strerror(errno) << "\n";
                ::close(sock_fd);
                continue;
            }

            chat::Packet conn_pkt = chat::make_packet(chat::CommandType::CONNECT,
                                                      chat::StatusCode::OK,
                                                      username,
                                                      "",
                                                      username);
            if (!send_packet_to_server(sock_fd, conn_pkt)) {
                std::cerr << "[error] Failed to send connect packet.\n";
                ::close(sock_fd);
                continue;
            }

            chat::Packet resp{};
            int rrc = recv_exact_pkt_bytes(sock_fd, &resp, chat::PACKET_SIZE_BYTES);
            if (rrc <= 0) {
                std::cerr << "[error] Did not receive connect response from server.\n";
                ::close(sock_fd);
                continue;
            }

            const chat::StatusCode status = chat::get_status(resp);
            const std::string resp_payload = chat::get_payload_str(resp);
            if (status != chat::StatusCode::OK) {
                std::cout << "[connect failed] " << resp_payload << "\n";
                ::close(sock_fd);
                continue;
            }

            g_client_fd = sock_fd;
            current_user = username;
            g_connected.store(true);
            std::cout << "[ok] " << resp_payload << "\n";

            chat::ReceiverContext rx_ctx{g_client_fd, &g_running, &g_connected};
            rx_thread = std::thread(chat::run_receiver_thread, rx_ctx);
            continue;
        }

        if (cmd == "shutdown") {
            if (g_connected.load() && g_client_fd >= 0) {
                chat::Packet shut_pkt = chat::make_packet(chat::CommandType::SHUTDOWN,
                                                          chat::StatusCode::OK,
                                                          current_user,
                                                          "",
                                                          "");
                send_packet_to_server(g_client_fd, shut_pkt);
            }
            g_connected.store(false);
            g_running.store(false);
            close_client_socket(g_client_fd);
            join_receiver_if_needed();
            current_user.clear();
            break;
        }

        if (!g_connected.load() || g_client_fd < 0) {
            std::cout << "[info] Not connected. Use: connect <server IP> <user>\n";
            continue;
        }

        if (cmd == "sendmessage") {
            std::string target_user;
            iss >> target_user;
            std::string msg_body;
            std::getline(iss, msg_body);
            msg_body = trim_ws(msg_body);

            if (target_user.empty() || msg_body.empty()) {
                std::cout << "[usage] sendmessage <user> <msg>\n";
                continue;
            }

            chat::Packet pkt = chat::make_packet(chat::CommandType::SEND_MESSAGE,
                                                 chat::StatusCode::OK,
                                                 current_user,
                                                 target_user,
                                                 msg_body);
            if (!send_packet_to_server(g_client_fd, pkt)) {
                std::cerr << "[error] Failed to send message.\n";
                g_connected.store(false);
            }
        } else if (cmd == "getmessages") {
            std::string target_user;
            iss >> target_user;
            if (target_user.empty()) {
                std::cout << "[usage] getmessages <user>\n";
                continue;
            }

            chat::Packet pkt = chat::make_packet(chat::CommandType::GET_MESSAGES,
                                                 chat::StatusCode::OK,
                                                 current_user,
                                                 target_user,
                                                 "");
            if (!send_packet_to_server(g_client_fd, pkt)) {
                std::cerr << "[error] Failed to send getmessages request.\n";
                g_connected.store(false);
            }
        } else if (cmd == "deletemessages") {
            std::string target_user;
            iss >> target_user;
            if (target_user.empty()) {
                std::cout << "[usage] deletemessages <user>\n";
                continue;
            }

            chat::Packet pkt = chat::make_packet(chat::CommandType::DELETE_MESSAGES,
                                                 chat::StatusCode::OK,
                                                 current_user,
                                                 target_user,
                                                 "");
            if (!send_packet_to_server(g_client_fd, pkt)) {
                std::cerr << "[error] Failed to send deletemessages request.\n";
                g_connected.store(false);
            }
        } else if (cmd == "getuserlist") {
            chat::Packet pkt = chat::make_packet(chat::CommandType::GET_USER_LIST,
                                                 chat::StatusCode::OK,
                                                 current_user,
                                                 "",
                                                 "");
            if (!send_packet_to_server(g_client_fd, pkt)) {
                std::cerr << "[error] Failed to send getuserlist request.\n";
                g_connected.store(false);
            }
        } else {
            std::cout << "[error] Unknown command '" << cmd << "'. Type 'help'.\n";
        }
    }

    g_connected.store(false);
    g_running.store(false);
    close_client_socket(g_client_fd);
    join_receiver_if_needed();

    std::cout << "[client] Exited cleanly.\n";
    return 0;
}
