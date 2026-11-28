/**
 * @file command_handler.cpp
 * @brief Implements the server's command set.
 */
#include "command_handler.h"

#include <mutex>
#include <sstream>

#include "console.h"
#include "db.h"
#include "user_registry.h"

namespace chat {

namespace {

void reply_ok(const SessionPtr &s, const std::string &text) {
    s->send(create_packet(ResponseType::RESP_SUCCESS, "server", s->username(), text));
}

void reply_error(const SessionPtr &s, StatusCode code, const std::string &text) {
    s->send(create_error_packet(code, text));
}

/** Common check for commands that take a <user> argument. */
bool require_valid_target(const ServerContext &ctx, const SessionPtr &s,
                          const std::string &target, const char *command) {
    if (target.empty()) {
        reply_error(s, StatusCode::ERR_GENERAL, std::string("Usage: ") + command + " <user>");
        return false;
    }
    if (!ctx.users->is_valid(target)) {
        reply_error(s, StatusCode::ERR_INVALID_USER,
                    "Invalid user '" + target + "': not in the list of valid users.");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- sendmessage
void handle_send_message(const ServerContext &ctx, const SessionPtr &s, const Packet &req) {
    const std::string target = req.get_recipient();
    const std::string text = req.get_payload();

    if (!require_valid_target(ctx, s, target, "sendmessage")) return;
    if (text.empty()) {
        reply_error(s, StatusCode::ERR_GENERAL, "Usage: sendmessage <user> <message>");
        return;
    }

    bool delivered = false;
    bool stored = false;
    {
        // Serialize with the recipient's login so the message is either pushed
        // live or stored as unread *before* their login drains the queue.
        std::lock_guard<std::mutex> delivery(ctx.sessions->delivery_lock(target));
        if (SessionPtr dest = ctx.sessions->find(target)) {
            delivered = dest->send(
                create_packet(ResponseType::MSG_REALTIME, s->username(), target, text));
        }
        stored = db_save_message(s->username(), target, text, delivered);
    }

    if (!stored) {
        reply_error(s, StatusCode::ERR_DATABASE_FAILURE, "Server could not store the message.");
        return;
    }
    log_event("Message " + s->username() + " -> " + target +
              (delivered ? " (delivered live)" : " (stored for offline user)"));
    reply_ok(s, delivered ? "Message delivered to " + target + "."
                          : target + " is offline. Message will be delivered at next login.");
}

// ---------------------------------------------------------------- getmessages
void handle_get_messages(const ServerContext &ctx, const SessionPtr &s, const Packet &req) {
    const std::string target = req.get_recipient();
    if (!require_valid_target(ctx, s, target, "getmessages")) return;

    const auto history = db_get_conversation(s->username(), target);
    if (history.empty()) {
        reply_ok(s, "No messages exchanged with " + target + ".");
        return;
    }
    // One packet per message so long histories are never truncated.
    s->send(create_packet(ResponseType::RESP_INFO, "server", s->username(),
                          "--- Conversation with " + target + " (" +
                              std::to_string(history.size()) + " messages) ---"));
    for (const auto &m : history) {
        std::string line = "[" + m.timestamp + "] " + m.sender + " -> " + m.recipient + ": " + m.content;
        s->send(create_packet(ResponseType::RESP_HISTORY, m.sender, m.recipient, line));
    }
    s->send(create_packet(ResponseType::RESP_INFO, "server", s->username(), "--- End of conversation ---"));
    log_event(s->username() + " retrieved history with " + target);
}

// ------------------------------------------------------------- deletemessages
void handle_delete_messages(const ServerContext &ctx, const SessionPtr &s, const Packet &req) {
    const std::string target = req.get_recipient();
    if (!require_valid_target(ctx, s, target, "deletemessages")) return;

    const int removed = db_delete_conversation(s->username(), target);
    if (removed < 0) {
        reply_error(s, StatusCode::ERR_DATABASE_FAILURE, "Failed to delete messages with " + target + ".");
        return;
    }
    log_event(s->username() + " deleted " + std::to_string(removed) + " message(s) with " + target);
    reply_ok(s, "Deleted " + std::to_string(removed) + " message(s) exchanged with " + target + ".");
}

// ---------------------------------------------------------------- getuserlist
void handle_user_list(const ServerContext &ctx, const SessionPtr &s) {
    const auto users = ctx.sessions->online_users();
    std::ostringstream oss;
    oss << "Online users (" << users.size() << "):";
    for (const auto &u : users) oss << "\n  " << u << (u == s->username() ? " (you)" : "");
    s->send(create_packet(ResponseType::RESP_USERLIST, "server", s->username(), oss.str()));
}

}  // namespace

void deliver_unread_messages(const SessionPtr &s) {
    const auto unread = db_get_unread_messages(s->username());
    if (unread.empty()) {
        s->send(create_packet(ResponseType::RESP_INFO, "server", s->username(), "No unread messages."));
        return;
    }
    s->send(create_packet(ResponseType::RESP_INFO, "server", s->username(),
                          "You have " + std::to_string(unread.size()) + " unread message(s):"));

    std::vector<int64_t> shown;
    for (const auto &m : unread) {
        if (!s->send(create_packet(ResponseType::RESP_UNREAD, m.sender, m.recipient, m.content))) break;
        shown.push_back(m.id);
    }
    db_mark_read(shown);  // only what actually reached the client
    log_event("Delivered " + std::to_string(shown.size()) + " unread message(s) to " + s->username());
}

bool handle_command(const ServerContext &ctx, const SessionPtr &session, const Packet &req) {
    switch (static_cast<CommandType>(req.type)) {
        case CommandType::CMD_SEND_MSG:     handle_send_message(ctx, session, req);   return true;
        case CommandType::CMD_GET_MSGS:     handle_get_messages(ctx, session, req);   return true;
        case CommandType::CMD_DEL_MSGS:     handle_delete_messages(ctx, session, req); return true;
        case CommandType::CMD_GET_USERLIST: handle_user_list(ctx, session);           return true;
        case CommandType::CMD_SHUTDOWN_CLIENT:
            reply_ok(session, "Disconnected from server. Goodbye " + session->username() + ".");
            return false;
        case CommandType::CMD_CONNECT:
            reply_error(session, StatusCode::ERR_GENERAL,
                        "Already connected as " + session->username() + ".");
            return true;
        default:
            reply_error(session, StatusCode::ERR_PROTOCOL, "Unknown command.");
            return true;
    }
}

}  // namespace chat
