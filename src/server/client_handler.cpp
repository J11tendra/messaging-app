#include "client_handler.hpp"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <unistd.h>

#include "database.hpp"
#include "protocol.hpp"
#include "session_manager.hpp"

namespace chat {

namespace {

// Ensures all `len` bytes are transmitted over a blocking TCP socket.
bool send_all(int fd, const void* buf, size_t len) {
    const auto* ptr = static_cast<const uint8_t*>(buf);
    size_t total_sent = 0;

    while (total_sent < len) {
        ssize_t n = ::send(fd, ptr + total_sent, len - total_sent, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (n == 0) {
            return false;
        }
        total_sent += static_cast<size_t>(n);
    }
    return true;
}

// Reads an exact `len`-byte frame from a TCP stream.
// Returns:  1 on full read, 0 on clean EOF, -1 on error.
int recv_exact(int fd, void* buf, size_t len) {
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

bool send_packet(int fd, const Packet& pkt) {
    return send_all(fd, &pkt, PACKET_SIZE_BYTES);
}

void cleanup_session(int client_fd, SessionManager* sessions, std::string& authenticated_user) {
    if (!authenticated_user.empty()) {
        if (sessions != nullptr) {
            sessions->remove_by_username(authenticated_user);
        }
        authenticated_user.clear();
    } else if (sessions != nullptr && client_fd >= 0) {
        sessions->remove_by_fd(client_fd);
    }

    if (client_fd >= 0) {
        ::shutdown(client_fd, SHUT_RDWR);
        ::close(client_fd);
    }
}

}  // namespace

void handle_client(ClientHandlerContext ctx) {
    const int client_fd = ctx.client_fd;
    SessionManager* sessions = ctx.sessions;
    Database* db = ctx.db;
    std::atomic<bool>* running = ctx.running;

    std::string authenticated_user;

    while (running == nullptr || running->load()) {
        Packet req{};
        int rc = recv_exact(client_fd, &req, PACKET_SIZE_BYTES);
        if (rc == 0) {
            // Client closed connection
            break;
        }
        if (rc < 0) {
            if (running != nullptr && !running->load()) {
                break;
            }
            std::cerr << "[worker] recv error on fd " << client_fd
                      << ": " << std::strerror(errno) << "\n";
            break;
        }

        const CommandType cmd = get_command(req);
        const std::string sender_field = get_sender_str(req);
        const std::string recipient_field = get_recipient_str(req);
        const std::string payload_str = get_payload_str(req);

        if (cmd == CommandType::CONNECT) {
            const std::string target_user = !sender_field.empty() ? sender_field : payload_str;
            if (target_user.empty() || target_user.size() >= MAX_USERNAME_LEN) {
                Packet resp = make_packet(CommandType::CONNECT,
                                          StatusCode::ERR_INVALID_USER,
                                          "server",
                                          target_user,
                                          "Invalid username.");
                send_packet(client_fd, resp);
                continue;
            }

            if (sessions != nullptr && sessions->is_online(target_user)) {
                Packet resp = make_packet(CommandType::CONNECT,
                                          StatusCode::ERR_DUPLICATE_LOGIN,
                                          "server",
                                          target_user,
                                          "User is already logged in.");
                send_packet(client_fd, resp);
                continue;
            }

            std::string db_err;
            if (db != nullptr && !db->authenticate_or_register_user(target_user, &db_err)) {
                Packet resp = make_packet(CommandType::CONNECT,
                                          StatusCode::ERR_DB_ERROR,
                                          "server",
                                          target_user,
                                          "Authentication failed: " + db_err);
                send_packet(client_fd, resp);
                continue;
            }

            if (sessions == nullptr || !sessions->add_session(target_user, client_fd)) {
                Packet resp = make_packet(CommandType::CONNECT,
                                          StatusCode::ERR_DUPLICATE_LOGIN,
                                          "server",
                                          target_user,
                                          "Could not register active session (duplicate login).");
                send_packet(client_fd, resp);
                continue;
            }

            authenticated_user = target_user;
            Packet ack = make_packet(CommandType::CONNECT,
                                     StatusCode::OK,
                                     "server",
                                     authenticated_user,
                                     "Connected as " + authenticated_user);
            if (!send_packet(client_fd, ack)) {
                break;
            }

            // Replay any queued offline messages (is_read == 0)
            if (db != nullptr) {
                std::vector<StoredMessage> unread =
                    db->fetch_and_mark_unread(authenticated_user, &db_err);
                for (const auto& msg : unread) {
                    std::string formatted = "[" + msg.timestamp + "] " + msg.sender + ": " + msg.body;
                    Packet offline_pkt = make_packet(CommandType::SEND_MESSAGE,
                                                     StatusCode::OK,
                                                     msg.sender,
                                                     authenticated_user,
                                                     formatted);
                    if (!send_packet(client_fd, offline_pkt)) {
                        goto done;
                    }
                }
            }
            continue;
        }

        // Require authentication before processing any other command
        if (authenticated_user.empty()) {
            Packet resp = make_packet(cmd,
                                      StatusCode::ERR_BAD_REQUEST,
                                      "server",
                                      "",
                                      "Must connect with a username first.");
            send_packet(client_fd, resp);
            continue;
        }

        switch (cmd) {
            case CommandType::SEND_MESSAGE: {
                if (recipient_field.empty() || payload_str.empty()) {
                    Packet resp = make_packet(CommandType::SEND_MESSAGE,
                                              StatusCode::ERR_BAD_REQUEST,
                                              "server",
                                              authenticated_user,
                                              "Recipient and message body are required.");
                    send_packet(client_fd, resp);
                    break;
                }

                bool recipient_online = false;
                int recipient_fd = -1;
                if (sessions != nullptr) {
                    auto fd_opt = sessions->get_fd(recipient_field);
                    if (fd_opt.has_value()) {
                        recipient_online = true;
                        recipient_fd = *fd_opt;
                    }
                }

                std::string db_err;
                bool stored = (db == nullptr) || db->store_message(authenticated_user,
                                                                    recipient_field,
                                                                    payload_str,
                                                                    recipient_online,
                                                                    &db_err);
                if (!stored) {
                    Packet resp = make_packet(CommandType::SEND_MESSAGE,
                                              StatusCode::ERR_DB_ERROR,
                                              "server",
                                              authenticated_user,
                                              "Failed to persist message: " + db_err);
                    send_packet(client_fd, resp);
                    break;
                }

                if (recipient_online && recipient_fd >= 0) {
                    Packet forward_pkt = make_packet(CommandType::SEND_MESSAGE,
                                                     StatusCode::OK,
                                                     authenticated_user,
                                                     recipient_field,
                                                     payload_str);
                    send_packet(recipient_fd, forward_pkt);

                    Packet ack = make_packet(CommandType::SEND_MESSAGE,
                                             StatusCode::OK,
                                             "server",
                                             authenticated_user,
                                             "Delivered to " + recipient_field);
                    send_packet(client_fd, ack);
                } else {
                    Packet ack = make_packet(CommandType::SEND_MESSAGE,
                                             StatusCode::OK,
                                             "server",
                                             authenticated_user,
                                             "Queued offline for " + recipient_field);
                    send_packet(client_fd, ack);
                }
                break;
            }

            case CommandType::GET_MESSAGES: {
                if (recipient_field.empty()) {
                    Packet resp = make_packet(CommandType::GET_MESSAGES,
                                              StatusCode::ERR_BAD_REQUEST,
                                              "server",
                                              authenticated_user,
                                              "Target user is required for getmessages.");
                    send_packet(client_fd, resp);
                    break;
                }

                std::string db_err;
                std::vector<StoredMessage> history =
                    (db != nullptr)
                        ? db->get_conversation_history(authenticated_user, recipient_field, &db_err)
                        : std::vector<StoredMessage>{};

                if (!db_err.empty()) {
                    Packet resp = make_packet(CommandType::GET_MESSAGES,
                                              StatusCode::ERR_DB_ERROR,
                                              "server",
                                              authenticated_user,
                                              "Failed to fetch history: " + db_err);
                    send_packet(client_fd, resp);
                    break;
                }

                std::ostringstream oss;
                if (history.empty()) {
                    oss << "No messages exchanged with " << recipient_field << ".";
                } else {
                    for (const auto& m : history) {
                        oss << "[" << m.timestamp << "] " << m.sender
                            << " -> " << m.recipient << ": " << m.body << "\n";
                    }
                }

                Packet resp = make_packet(CommandType::GET_MESSAGES,
                                          StatusCode::OK,
                                          "server",
                                          authenticated_user,
                                          oss.str());
                send_packet(client_fd, resp);
                break;
            }

            case CommandType::DELETE_MESSAGES: {
                if (recipient_field.empty()) {
                    Packet resp = make_packet(CommandType::DELETE_MESSAGES,
                                              StatusCode::ERR_BAD_REQUEST,
                                              "server",
                                              authenticated_user,
                                              "Target user is required for deletemessages.");
                    send_packet(client_fd, resp);
                    break;
                }

                std::string db_err;
                bool ok = (db == nullptr) ||
                          db->delete_conversation_history(authenticated_user, recipient_field, &db_err);
                if (!ok) {
                    Packet resp = make_packet(CommandType::DELETE_MESSAGES,
                                              StatusCode::ERR_DB_ERROR,
                                              "server",
                                              authenticated_user,
                                              "Failed to delete messages: " + db_err);
                    send_packet(client_fd, resp);
                    break;
                }

                Packet resp = make_packet(CommandType::DELETE_MESSAGES,
                                          StatusCode::OK,
                                          "server",
                                          authenticated_user,
                                          "Deleted message history with " + recipient_field);
                send_packet(client_fd, resp);
                break;
            }

            case CommandType::GET_USER_LIST: {
                std::ostringstream oss;
                if (sessions != nullptr) {
                    std::vector<std::string> users = sessions->get_online_users();
                    for (size_t i = 0; i < users.size(); ++i) {
                        if (i > 0) {
                            oss << ", ";
                        }
                        oss << users[i];
                    }
                }

                Packet resp = make_packet(CommandType::GET_USER_LIST,
                                          StatusCode::OK,
                                          "server",
                                          authenticated_user,
                                          oss.str());
                send_packet(client_fd, resp);
                break;
            }

            case CommandType::SHUTDOWN: {
                Packet ack = make_packet(CommandType::SHUTDOWN,
                                         StatusCode::OK,
                                         "server",
                                         authenticated_user,
                                         "Disconnected.");
                send_packet(client_fd, ack);
                goto done;
            }

            case CommandType::CONNECT:
            case CommandType::SERVER_NOTICE:
            default: {
                Packet resp = make_packet(cmd,
                                          StatusCode::ERR_BAD_REQUEST,
                                          "server",
                                          authenticated_user,
                                          "Unsupported command.");
                send_packet(client_fd, resp);
                break;
            }
        }
    }

done:
    cleanup_session(client_fd, sessions, authenticated_user);
}

}  // namespace chat
