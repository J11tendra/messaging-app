/**
 * @file client_main.cpp
 * @brief Client process: CLI thread + receiver thread.
 *
 * Usage: ./client [-p port] [<server IP> <user Name>]
 *   With IP and name it connects immediately; otherwise type
 *   "connect <server IP> <user Name>" at the prompt.
 *
 * Threads
 *   main thread     poll()s stdin (user commands) and a wake-up pipe.
 *   receiver thread blocks in recv() and prints incoming messages immediately.
 * The wake-up pipe is written by the receiver thread when the server goes away
 * and by the SIGINT handler (self-pipe trick), so the main thread never stays
 * stuck waiting for keyboard input after the server has shut down.
 *
 * Nothing is stored locally: all chat data lives on the server.
 */
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

#include "client_commands.h"
#include "console.h"
#include "network.h"
#include "protocol.h"
#include "receiver_thread.h"

namespace {

int g_wake_pipe[2] = {-1, -1};

extern "C" void on_sigint(int) {
    const int saved = errno;
    const char c = 'S';
    ssize_t ignored = ::write(g_wake_pipe[1], &c, 1);
    (void)ignored;
    errno = saved;
}

class Client {
public:
    explicit Client(uint16_t port) : port_(port) {}

    /** Returns false if the client must exit (server rejected the login). */
    bool connect(const std::string &ip, const std::string &user);

    /** Sends "shutdown", waits briefly for the server's goodbye, tears down. */
    void disconnect();

    /** Tears down after the server went away (no shutdown packet sent). */
    void cleanup_after_server_exit();

    bool connected() const { return fd_ >= 0; }
    bool send(const chat::Packet &pkt) { return chat::send_packet(fd_, pkt); }
    const std::string &user() const { return user_; }
    bool server_gone() const { return server_gone_.load(); }

private:
    void stop_receiver();

    uint16_t port_;
    int fd_{-1};
    std::string user_;
    std::thread receiver_;
    std::atomic<bool> server_gone_{false};
    std::atomic<bool> quitting_{false};
};

bool Client::connect(const std::string &ip, const std::string &user) {
    if (connected()) {
        chat::console_print("[info] Already connected as " + user_ + ". Use 'shutdown' first.");
        return true;
    }
    std::string err;
    int fd = chat::connect_to_server(ip, port_, err);
    if (fd < 0) {
        chat::console_print("[error] Connection failed: " + err);
        return true;  // network problem: let the user retry
    }

    chat::Packet req;
    chat::build_request(chat::parse_command("connect " + ip + " " + user), user, req);
    chat::Packet resp;
    if (!chat::send_packet(fd, req) || !chat::recv_packet(fd, resp)) {
        chat::console_print("[error] Server closed the connection during login.");
        chat::close_socket(fd);
        return true;
    }
    if (resp.type != static_cast<uint16_t>(chat::ResponseType::RESP_SUCCESS)) {
        // Invalid user / duplicate session / server full: show the reason and exit.
        chat::console_print("[login rejected] " + resp.get_payload(), false);
        chat::close_socket(fd);
        return false;
    }

    fd_ = fd;
    user_ = user;
    server_gone_ = false;
    quitting_ = false;
    chat::console_print("[ok] " + resp.get_payload(), false);
    // Unread messages follow on the socket; the receiver thread prints them.
    receiver_ = std::thread(chat::run_receiver_thread,
                            chat::ReceiverContext{fd_, &server_gone_, &quitting_, g_wake_pipe[1]});
    return true;
}

void Client::stop_receiver() {
    if (fd_ >= 0) ::shutdown(fd_, SHUT_RDWR);  // unblocks recv() in the receiver
    if (receiver_.joinable()) receiver_.join();
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

void Client::disconnect() {
    if (!connected()) return;
    quitting_ = true;
    if (!server_gone_ && send(chat::create_packet(chat::CommandType::CMD_SHUTDOWN_CLIENT, user_))) {
        // Give the receiver up to 2 s to print the server's goodbye and see EOF.
        pollfd p{g_wake_pipe[0], POLLIN, 0};
        for (int waited = 0; waited < 20 && !(::poll(&p, 1, 100) > 0); ++waited) {}
    }
    stop_receiver();
}

void Client::cleanup_after_server_exit() { stop_receiver(); }

/** Reads whatever is available on stdin and extracts complete lines. */
bool read_stdin_lines(std::string &buf, std::string &line_out, bool &eof) {
    size_t nl = buf.find('\n');
    if (nl == std::string::npos) {
        char tmp[512];
        ssize_t n = ::read(STDIN_FILENO, tmp, sizeof(tmp));
        if (n <= 0) {
            if (n < 0 && errno == EINTR) return false;
            eof = true;
            if (buf.empty()) return false;
            line_out.swap(buf);  // last line without newline
            buf.clear();
            return true;
        }
        buf.append(tmp, static_cast<size_t>(n));
        nl = buf.find('\n');
        if (nl == std::string::npos) return false;
    }
    line_out = buf.substr(0, nl);
    buf.erase(0, nl + 1);
    return true;
}

}  // namespace

int main(int argc, char *argv[]) {
    uint16_t port = chat::DEFAULT_PORT;
    int argi = 1;
    if (argi + 1 < argc && std::string(argv[argi]) == "-p") {
        port = static_cast<uint16_t>(std::atoi(argv[argi + 1]));
        argi += 2;
    }

    if (::pipe(g_wake_pipe) < 0) return 1;
    ::fcntl(g_wake_pipe[1], F_SETFL, O_NONBLOCK);
    struct sigaction sa{};
    sa.sa_handler = on_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
    std::signal(SIGPIPE, SIG_IGN);

    std::cout << "======================================================\n"
              << "   Multi-User Messaging Client\n"
              << "======================================================\n"
              << chat::help_text() << "\n"
              << "------------------------------------------------------\n";

    Client client(port);
    if (argc - argi >= 2) {
        if (!client.connect(argv[argi], argv[argi + 1])) return 1;
    }

    std::string stdin_buf;
    bool stdin_eof = false;
    int exit_code = 0;
    chat::console_prompt();

    while (true) {
        pollfd fds[2] = {{STDIN_FILENO, POLLIN, 0}, {g_wake_pipe[0], POLLIN, 0}};
        if (::poll(fds, 2, -1) < 0) {
            if (errno == EINTR) continue;
            break;
        }

        if (fds[1].revents & POLLIN) {
            char c = 0;
            if (::read(g_wake_pipe[0], &c, 1) > 0) {
                if (c == 'S') {  // Ctrl+C
                    chat::console_print("\n[client] Interrupted, disconnecting...", false);
                    client.disconnect();
                    break;
                }
                if (c == 'R' && client.server_gone()) {
                    client.cleanup_after_server_exit();
                    chat::console_print("[client] Server terminated. Client exiting.", false);
                    break;
                }
            }
        }

        if (!(fds[0].revents & (POLLIN | POLLHUP))) continue;
        std::string line;
        if (!read_stdin_lines(stdin_buf, line, stdin_eof)) {
            if (stdin_eof) {  // end of input = shutdown
                client.disconnect();
                break;
            }
            continue;
        }

        chat::ParsedCommand cmd = chat::parse_command(line);
        switch (cmd.kind) {
            case chat::ClientCommand::Empty:
                chat::console_prompt();
                continue;
            case chat::ClientCommand::Help:
                chat::console_print(chat::help_text());
                continue;
            case chat::ClientCommand::Unknown:
            case chat::ClientCommand::Invalid:
                chat::console_print("[error] " + cmd.error);
                continue;
            case chat::ClientCommand::Connect:
                if (!client.connect(cmd.arg1, cmd.arg2)) {
                    exit_code = 1;
                    goto done;
                }
                chat::console_prompt();
                continue;
            case chat::ClientCommand::Shutdown:
                client.disconnect();
                chat::console_print("[client] Disconnected. Bye.", false);
                goto done;
            default:
                break;
        }

        if (!client.connected()) {
            chat::console_print("[info] Not connected. Use: connect <server IP> <user Name>");
            continue;
        }
        chat::Packet req;
        if (chat::build_request(cmd, client.user(), req) && !client.send(req)) {
            chat::console_print("[error] Could not send request: connection lost.");
        }
        // The reply is printed by the receiver thread.
    }
done:
    if (client.connected()) client.disconnect();
    return exit_code;
}
