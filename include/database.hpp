#pragma once

#include <mutex>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace chat {

struct StoredMessage {
    int64_t id = 0;
    std::string sender;
    std::string recipient;
    std::string body;
    std::string timestamp;
    bool is_read = false;
};

class Database {
public:
    explicit Database(const std::string& db_path = "data/server.db");
    ~Database();

    // Non-copyable and non-movable due to SQLite handle and mutex
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) = delete;
    Database& operator=(Database&&) = delete;

    // Opens the SQLite database and creates tables if they do not exist.
    bool init();

    // Closes the database cleanly (used during shutdown/destruction).
    void close();

    // Authenticates an existing user or registers a new authorized user if allowed.
    // Returns true on success; sets `err_msg` on failure.
    bool authenticate_or_register_user(const std::string& username, std::string* err_msg = nullptr);

    // Persists a chat message with `is_read` set to `delivered_immediately`.
    bool store_message(const std::string& sender,
                       const std::string& recipient,
                       const std::string& body,
                       bool delivered_immediately,
                       std::string* err_msg = nullptr);

    // Fetches unread messages addressed to `username` (is_read == 0) and marks them read.
    std::vector<StoredMessage> fetch_and_mark_unread(const std::string& username,
                                                       std::string* err_msg = nullptr);

    // Fetches full chronological two-way chat history between `user_a` and `user_b`.
    std::vector<StoredMessage> get_conversation_history(const std::string& user_a,
                                                        const std::string& user_b,
                                                        std::string* err_msg = nullptr);

    // Permanently deletes all two-way messages exchanged between `user_a` and `user_b`.
    bool delete_conversation_history(const std::string& user_a,
                                     const std::string& user_b,
                                     std::string* err_msg = nullptr);

private:
    bool create_schema(std::string* err_msg);

    std::string db_path_;
    sqlite3* db_ = nullptr;
    mutable std::mutex db_mtx_;
};

}  // namespace chat
