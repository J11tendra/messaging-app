#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

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
#include "receiver_thread.h"

namespace {

std::atomic<bool> g_running{true};
std::atomic<bool> g_connected{false};
int g_client_fd = -1;

void close_socket(int &fd) {
    if (fd >= 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
#endif
        fd = -1;
    }
}

void sigint_handler(int /*signum*/) {
    g_running.store(false);
    if (g_connected.load() && g_client_fd >= 0) {
        chat::Packet shut_pkt = chat::create_packet(
            static_cast<uint16_t>(chat::CommandType::CMD_SHUTDOWN_CLIENT)
        );
        chat::send_packet(g_client_fd, shut_pkt);
    }
    g_connected.store(false);
    close_socket(g_client_fd);
    std::cout << "\n[client] Exiting...\n";
    std::exit(0);
}

std::string trim(const std::string &s) {
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

void print_help() {
    std::cout << "Available Commands:\n"
              << "  connect <server IP> <user Name> - Connect and login to server\n"
              << "  sendmessage <user> <message>    - Send real-time or offline message\n"
              << "  getmessages <user>              - Retrieve message history with user\n"
              << "  deletemessages <user>           - Delete all message history with user\n"
              << "  getuserlist                     - List all online connected users\n"
              << "  shutdown                        - Disconnect gracefully and exit\n"
              << "  help                            - Show this command reference\n";
}

bool do_connect(const std::string &server_ip,
                const std::string &username,
                uint16_t port,
                std::string &current_user,
                std::thread &rx_thread) {
    if (g_connected.load() || g_client_fd >= 0) {
        std::cout << "[info] Already connected as '" << current_user << "'. Use 'shutdown' to disconnect first.\n";
        return false;
    }

    if (rx_thread.joinable()) {
        rx_thread.join();
    }

    int sock_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        std::cerr << "[error] socket() failed: " << std::strerror(errno) << "\n";
        return false;
    }

    sockaddr_in srv_addr{};
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, server_ip.c_str(), &srv_addr.sin_addr) <= 0) {
        std::cerr << "[error] Invalid server IP address: " << server_ip << "\n";
        close_socket(sock_fd);
        return false;
    }

    if (::connect(sock_fd, reinterpret_cast<sockaddr*>(&srv_addr), sizeof(srv_addr)) < 0) {
        std::cerr << "[error] Failed to connect to server at " << server_ip << ":" << port
                  << " (" << std::strerror(errno) << ")\n";
        close_socket(sock_fd);
        return false;
    }

    // Send CMD_CONNECT packet
    chat::Packet conn_pkt = chat::create_packet(
        static_cast<uint16_t>(chat::CommandType::CMD_CONNECT),
        username,
        "",
        username
    );

    if (!chat::send_packet(sock_fd, conn_pkt)) {
        std::cerr << "[error] Failed to send connection handshake to server.\n";
        close_socket(sock_fd);
        return false;
    }

    // Wait for server handshake response
    chat::Packet resp;
    if (!chat::recv_packet(sock_fd, resp)) {
        std::cerr << "[error] Did not receive connection response from server.\n";
        close_socket(sock_fd);
        return false;
    }

    if (resp.type != static_cast<uint16_t>(chat::ResponseType::RESP_SUCCESS)) {
        std::cout << "[connect failed] " << resp.get_payload() << "\n";
        close_socket(sock_fd);
        return false;
    }

    g_client_fd = sock_fd;
    current_user = username;
    g_connected.store(true);
    std::cout << "[ok] " << resp.get_payload() << "\n";

    // Launch background receiver thread for non-blocking message listening
    chat::ReceiverContext rx_ctx{g_client_fd, &g_running, &g_connected};
    rx_thread = std::thread(chat::run_receiver_thread, rx_ctx);
    return true;
}

}  // namespace

int main(int argc, char *argv[]) {
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
    std::cout << "   Multi-User Messaging Client (Linux C++17)           \n";
    std::cout << "======================================================\n";
    print_help();
    std::cout << "------------------------------------------------------\n";

    std::string current_user;
    std::thread rx_thread;
    uint16_t default_port = chat::DEFAULT_PORT;

    // Support direct launch invocation: ./client <server_ip> <username> [port]
    if (argc >= 3) {
        std::string srv_ip = argv[1];
        std::string uname = argv[2];
        if (argc >= 4) {
            default_port = static_cast<uint16_t>(std::stoi(argv[3]));
        }
        std::cout << "[client] Connecting to " << srv_ip << ":" << default_port << " as '" << uname << "'...\n";
        do_connect(srv_ip, uname, default_port, current_user, rx_thread);
    }

    std::string line;
    while (g_running.load()) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) {
            break;
        }

        std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        std::istringstream iss(trimmed);
        std::string cmd;
        iss >> cmd;

        if (cmd == "help") {
            print_help();
            continue;
        }

        if (cmd == "connect") {
            std::string server_ip, username;
            iss >> server_ip >> username;

            if (server_ip.empty() || username.empty()) {
                std::cout << "[usage] connect <server IP> <user Name>\n";
                continue;
            }

            do_connect(server_ip, username, default_port, current_user, rx_thread);
            continue;
        }

        if (cmd == "shutdown") {
            if (g_connected.load() && g_client_fd >= 0) {
                chat::Packet shut_pkt = chat::create_packet(
                    static_cast<uint16_t>(chat::CommandType::CMD_SHUTDOWN_CLIENT),
                    current_user
                );
                chat::send_packet(g_client_fd, shut_pkt);
            }
            g_connected.store(false);
            g_running.store(false);
            close_socket(g_client_fd);
            if (rx_thread.joinable()) {
                rx_thread.join();
            }
            std::cout << "[client] Disconnected and shut down successfully.\n";
            break;
        }

        if (!g_connected.load() || g_client_fd < 0) {
            std::cout << "[info] You are not connected. Please connect first using: connect <server IP> <user Name>\n";
            continue;
        }

        if (cmd == "sendmessage") {
            std::string target;
            iss >> target;
            std::string msg;
            std::getline(iss, msg);
            msg = trim(msg);

            if (target.empty() || msg.empty()) {
                std::cout << "[usage] sendmessage <user> <message>\n";
                continue;
            }

            chat::Packet pkt = chat::create_packet(
                static_cast<uint16_t>(chat::CommandType::CMD_SEND_MSG),
                current_user,
                target,
                msg
            );

            if (!chat::send_packet(g_client_fd, pkt)) {
                std::cerr << "[error] Failed to send message (connection lost).\n";
                g_connected.store(false);
            }
        } else if (cmd == "getmessages") {
            std::string target;
            iss >> target;

            if (target.empty()) {
                std::cout << "[usage] getmessages <user>\n";
                continue;
            }

            chat::Packet pkt = chat::create_packet(
                static_cast<uint16_t>(chat::CommandType::CMD_GET_MSGS),
                current_user,
                target
            );

            if (!chat::send_packet(g_client_fd, pkt)) {
                std::cerr << "[error] Failed to send getmessages request.\n";
                g_connected.store(false);
            }
        } else if (cmd == "deletemessages") {
            std::string target;
            iss >> target;

            if (target.empty()) {
                std::cout << "[usage] deletemessages <user>\n";
                continue;
            }

            chat::Packet pkt = chat::create_packet(
                static_cast<uint16_t>(chat::CommandType::CMD_DEL_MSGS),
                current_user,
                target
            );

            if (!chat::send_packet(g_client_fd, pkt)) {
                std::cerr << "[error] Failed to send deletemessages request.\n";
                g_connected.store(false);
            }
        } else if (cmd == "getuserlist") {
            chat::Packet pkt = chat::create_packet(
                static_cast<uint16_t>(chat::CommandType::CMD_GET_USERLIST),
                current_user
            );

            if (!chat::send_packet(g_client_fd, pkt)) {
                std::cerr << "[error] Failed to send getuserlist request.\n";
                g_connected.store(false);
            }
        } else {
            std::cout << "[error] Unknown command '" << cmd << "'. Type 'help' to see valid commands.\n";
        }
    }

    g_connected.store(false);
    g_running.store(false);
    close_socket(g_client_fd);
    if (rx_thread.joinable()) {
        rx_thread.join();
    }

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
