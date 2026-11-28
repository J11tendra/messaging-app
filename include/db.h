/**
 * @file db.h
 * @brief Server-side persistent storage (SQLite). Only the server stores messages.
 *
 * All functions are thread-safe: one process-wide mutex serializes access to
 * the single sqlite3 connection, so concurrent worker threads never use the
 * connection at the same time.
 *
 * Schema
 *   users(username PK)
 *   messages(id PK, sender, recipient, content, timestamp, is_read)
 *     is_read = 0 -> recipient has not seen it yet (was offline) -> shown at next login
 *     is_read = 1 -> delivered in real time, or already shown at login
 */
#ifndef DB_H
#define DB_H

#include <cstdint>
#include <string>
#include <vector>

namespace chat {

struct StoredMessage {
    int64_t id{0};
    std::string sender;
    std::string recipient;
    std::string content;
    std::string timestamp;  // "YYYY-MM-DD HH:MM:SS" (server local time)
};

bool db_init(const std::string &db_path);
void db_close();

/** Ensures a row exists in users (needed for the messages foreign keys). */
bool db_add_user(const std::string &username);

/** Stores a message. delivered = true if it was pushed to an online recipient. */
bool db_save_message(const std::string &sender, const std::string &recipient,
                     const std::string &content, bool delivered);

/** Unread messages for a user, oldest first. Does NOT mark them read. */
std::vector<StoredMessage> db_get_unread_messages(const std::string &username);

/** Marks the given message ids as read (called after they reach the client). */
bool db_mark_read(const std::vector<int64_t> &ids);

/** All messages exchanged between two users, oldest first. */
std::vector<StoredMessage> db_get_conversation(const std::string &user1, const std::string &user2);

/** Deletes all messages to/from the pair. Returns rows deleted, or -1 on error. */
int db_delete_conversation(const std::string &user1, const std::string &user2);

}  // namespace chat

#endif  // DB_H
