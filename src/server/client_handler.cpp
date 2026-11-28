/**
 * @file client_handler.cpp
 * @brief Per-client worker thread: handshake + command loop + cleanup.
 */
#include "client_handler.h"

#include <mutex>

#include "command_handler.h"
#include "connection_tracker.h"
#include "console.h"
#include "network.h"
#include "session_manager.h"
#include "user_registry.h"

namespace chat {

namespace {

constexpr int HANDSHAKE_TIMEOUT_SEC = 15;

void reject(int fd, StatusCode code, const std::string &reason) {
    send_packet(fd, create_error_packet(code, reason));  // session not created yet: no lock needed
}

/** Validates the connect packet and registers the session. Returns nullptr on rejection. */
SessionPtr login(const ServerContext &ctx, int fd, const std::string &peer) {
    Packet req;
    set_recv_timeout(fd, HANDSHAKE_TIMEOUT_SEC);  // don't let a silent socket hold a thread
    const bool got = recv_packet(fd, req);
    set_recv_timeout(fd, 0);

    if (!got) {
        log_event("Connection from " + peer + " closed before login");
        return nullptr;
    }
    if (req.type != static_cast<uint16_t>(CommandType::CMD_CONNECT)) {
        reject(fd, StatusCode::ERR_PROTOCOL, "First request must be: connect <server IP> <user Name>");
        return nullptr;
    }

    const std::string username = req.get_sender();
    if (!ctx.users->is_valid(username)) {
        log_event("Login REJECTED from " + peer + ": invalid user '" + username + "'");
        reject(fd, StatusCode::ERR_INVALID_USER,
               "Invalid user '" + username + "'. This user is not allowed to log in.");
        return nullptr;
    }

    auto session = std::make_shared<Session>(username, fd);

    // Held across "register" + "drain unread" (see session_manager.h).
    std::lock_guard<std::mutex> delivery(ctx.sessions->delivery_lock(username));

    switch (ctx.sessions->add(session, ctx.max_clients)) {
        case SessionManager::AddResult::AlreadyConnected:
            log_event("Login REJECTED from " + peer + ": '" + username + "' already has a session");
            reject(fd, StatusCode::ERR_USER_ALREADY_CONNECTED,
                   "User '" + username + "' is already connected. Duplicate session rejected.");
            return nullptr;
        case SessionManager::AddResult::ServerFull:
            log_event("Login REJECTED from " + peer + ": server full");
            reject(fd, StatusCode::ERR_SERVER_FULL, "Server is full. Try again later.");
            return nullptr;
        case SessionManager::AddResult::Added:
            break;
    }

    log_event("User '" + username + "' connected from " + peer +
              " (online: " + std::to_string(ctx.sessions->count()) + ")");
    session->send(create_packet(ResponseType::RESP_SUCCESS, "server", username,
                                "Login successful. Welcome, " + username + "!"));
    deliver_unread_messages(session);
    return session;
}

}  // namespace

void handle_client(int fd, std::string peer, ServerContext ctx) {
    SessionPtr session = login(ctx, fd, peer);

    if (session) {
        Packet req;
        while (ctx.running->load() && recv_packet(fd, req)) {
            if (!handle_command(ctx, session, req)) break;  // client sent "shutdown"
        }
        // Cleanup: remove from online list first, then stop all writes, then free fd.
        ctx.sessions->remove(session);
        session->mark_closed();
        log_event("User '" + session->username() + "' disconnected (online: " +
                  std::to_string(ctx.sessions->count()) + ")");
    }
    ctx.connections->close_fd(fd);
}

}  // namespace chat
