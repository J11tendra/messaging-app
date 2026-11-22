#include "client_handler.h"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>

#ifdef _WIN32
    #include <winsock2.h>
#else
    #include <sys/socket.h>
    #include <unistd.h>
#endif

#include "protocol.h"
#include "db.h"
#include "session_manager.h"

namespace chat {

static void close_socket(int fd) {
    if (fd >= 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
#endif
    }
}

static bool is_valid_username(const std::string &name) {
    if (name.empty() || name.size() >= MAX_USERNAME_LEN) {
        return false;
    }
    if (name == "server" || name == "all") {
        return false;
    }
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != '.') {
            return false;
        }
    }
    return true;
}

void handle_client(ClientHandlerContext ctx) {
    const int client_fd = ctx.client_fd;
    SessionManager *sessions = ctx.sessions;
    std::atomic<bool> *running = ctx.running;

    std::string username;
    Packet req;

    // 1. Connection Handshake: The first packet MUST be CMD_CONNECT
    if (!recv_packet(client_fd, req)) {
        close_socket(client_fd);
        return;
    }

    if (req.type != static_cast<uint16_t>(CommandType::CMD_CONNECT)) {
        Packet err = create_error_packet(StatusCode::ERR_INVALID_USER, "Expected connect command.");
        send_packet(client_fd, err);
        close_socket(client_fd);
        return;
    }

    username = req.get_sender();
    if (username.empty()) {
        username = req.get_payload();
    }

    // Validate username syntax and character constraints
    if (!is_valid_username(username)) {
        std::cout << "[server] Connection rejected: invalid username format '" << username << "'\n";
        Packet err = create_error_packet(StatusCode::ERR_INVALID_USER,
                                         "Invalid username: must be 1-31 alphanumeric characters.");
        send_packet(client_fd, err);
        close_socket(client_fd);
        return;
    }

    // Authenticate and register new clients per project specification:
    // "The server authenticates and registers each new client with the provided username."
    if (!db_is_valid_user(username)) {
        if (!db_add_user(username)) {
            std::cout << "[server] Failed to register user '" << username << "' in database\n";
            Packet err = create_error_packet(StatusCode::ERR_DATABASE_FAILURE,
                                             "Failed to register user in database.");
            send_packet(client_fd, err);
            close_socket(client_fd);
            return;
        }
        std::cout << "[server] Registered new client '" << username << "' in user directory.\n";
    }

    // Check for duplicate active session
    if (sessions && sessions->is_online(username)) {
        std::cout << "[server] Connection rejected: user '" << username << "' is already logged in\n";
        Packet err = create_error_packet(StatusCode::ERR_USER_ALREADY_CONNECTED,
                                         "User '" + username + "' is already connected from another session.");
        send_packet(client_fd, err);
        close_socket(client_fd);
        return;
    }

    // Register active session
    if (!sessions || !sessions->add_session(username, client_fd)) {
        Packet err = create_error_packet(StatusCode::ERR_GENERAL, "Failed to register session.");
        send_packet(client_fd, err);
        close_socket(client_fd);
        return;
    }

    std::cout << "[server] User '" << username << "' connected successfully (socket fd=" << client_fd << ")\n";

    // Send successful connection acknowledgement
    Packet ack = create_packet(static_cast<uint16_t>(ResponseType::RESP_SUCCESS),
                               "server",
                               username,
                               "Connected successfully as " + username + ".");
    if (!send_packet(client_fd, ack)) {
        goto cleanup;
    }

    // Check for and deliver any offline queued messages (is_read = 0)
    {
        std::vector<Packet> unread = db_get_unread_messages(username);
        if (!unread.empty()) {
            std::cout << "[server] Delivering " << unread.size() << " queued offline messages to '" << username << "'\n";
            for (const auto &un_pkt : unread) {
                if (!send_packet(client_fd, un_pkt)) {
                    goto cleanup;
                }
            }
        }
    }

    // 2. Command Processing Loop
    while (running && running->load()) {
        if (!recv_packet(client_fd, req)) {
            // Client closed connection or network error
            break;
        }

        const auto cmd = static_cast<CommandType>(req.type);

        switch (cmd) {
            case CommandType::CMD_SEND_MSG: {
                std::string target = req.get_recipient();
                std::string text = req.get_payload();

                if (target.empty() || text.empty()) {
                    Packet err = create_error_packet(StatusCode::ERR_GENERAL,
                                                     "Recipient and message text are required.");
                    send_packet(client_fd, err);
                    break;
                }

                // Check if target user exists in database
                if (!db_is_valid_user(target)) {
                    Packet err = create_error_packet(StatusCode::ERR_INVALID_USER,
                                                     "User '" + target + "' does not exist.");
                    send_packet(client_fd, err);
                    break;
                }

                // Check if target user is currently online
                int target_fd = sessions ? sessions->get_socket(target) : -1;
                if (target_fd >= 0) {
                    // Online: persist as read (delivered in real time)
                    db_save_message(username, target, text, true);

                    // Deliver real-time message to target socket
                    Packet fwd = create_packet(static_cast<uint16_t>(ResponseType::MSG_REALTIME),
                                               username,
                                               target,
                                               text);
                    send_packet(target_fd, fwd);

                    // Acknowledge to sender
                    Packet res = create_packet(static_cast<uint16_t>(ResponseType::RESP_SUCCESS),
                                               "server",
                                               username,
                                               "Message delivered to " + target + ".");
                    send_packet(client_fd, res);
                } else {
                    // Offline: persist as unread (queued in database for next login)
                    db_save_message(username, target, text, false);

                    Packet res = create_packet(static_cast<uint16_t>(ResponseType::RESP_SUCCESS),
                                               "server",
                                               username,
                                               "User '" + target + "' is offline. Message queued for delivery at next login.");
                    send_packet(client_fd, res);
                }
                break;
            }

            case CommandType::CMD_GET_MSGS: {
                std::string target = req.get_recipient();
                if (target.empty()) {
                    Packet err = create_error_packet(StatusCode::ERR_GENERAL,
                                                     "Target user is required for getmessages.");
                    send_packet(client_fd, err);
                    break;
                }

                if (!db_is_valid_user(target)) {
                    Packet err = create_error_packet(StatusCode::ERR_INVALID_USER,
                                                     "User '" + target + "' does not exist.");
                    send_packet(client_fd, err);
                    break;
                }

                std::string conversation = db_get_conversation(username, target);
                Packet res = create_packet(static_cast<uint16_t>(ResponseType::RESP_HISTORY),
                                           "server",
                                           username,
                                           conversation);
                send_packet(client_fd, res);
                break;
            }

            case CommandType::CMD_DEL_MSGS: {
                std::string target = req.get_recipient();
                if (target.empty()) {
                    Packet err = create_error_packet(StatusCode::ERR_GENERAL,
                                                     "Target user is required for deletemessages.");
                    send_packet(client_fd, err);
                    break;
                }

                if (!db_is_valid_user(target)) {
                    Packet err = create_error_packet(StatusCode::ERR_INVALID_USER,
                                                     "User '" + target + "' does not exist.");
                    send_packet(client_fd, err);
                    break;
                }

                if (db_delete_conversation(username, target)) {
                    Packet res = create_packet(static_cast<uint16_t>(ResponseType::RESP_SUCCESS),
                                               "server",
                                               username,
                                               "All messages exchanged with '" + target + "' deleted successfully.");
                    send_packet(client_fd, res);
                } else {
                    Packet err = create_error_packet(StatusCode::ERR_DATABASE_FAILURE,
                                                     "Failed to delete messages with '" + target + "'.");
                    send_packet(client_fd, err);
                }
                break;
            }

            case CommandType::CMD_GET_USERLIST: {
                std::ostringstream oss;
                if (sessions) {
                    std::vector<std::string> users = sessions->get_online_users();
                    oss << "Online users (" << users.size() << "): ";
                    for (size_t i = 0; i < users.size(); ++i) {
                        if (i > 0) oss << ", ";
                        oss << users[i];
                    }
                }
                Packet res = create_packet(static_cast<uint16_t>(ResponseType::RESP_USERLIST),
                                           "server",
                                           username,
                                           oss.str());
                send_packet(client_fd, res);
                break;
            }

            case CommandType::CMD_SHUTDOWN_CLIENT: {
                Packet res = create_packet(static_cast<uint16_t>(ResponseType::RESP_SUCCESS),
                                           "server",
                                           username,
                                           "Disconnected gracefully.");
                send_packet(client_fd, res);
                goto cleanup;
            }

            case CommandType::CMD_CONNECT:
            default: {
                Packet err = create_error_packet(StatusCode::ERR_GENERAL, "Unknown or unexpected command.");
                send_packet(client_fd, err);
                break;
            }
        }
    }

cleanup:
    if (!username.empty() && sessions) {
        sessions->remove_by_username(username);
        std::cout << "[server] User '" << username << "' disconnected.\n";
    } else if (sessions) {
        sessions->remove_by_fd(client_fd);
    }
    close_socket(client_fd);
}

}  // namespace chat
