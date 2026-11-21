#include "database.hpp"

#include <cstdio>

namespace chat {

namespace {

void set_error(std::string* err_msg, const std::string& msg) {
    if (err_msg != nullptr) {
        *err_msg = msg;
    }
}

std::string column_text(sqlite3_stmt* stmt, int col) {
    const unsigned char* text = sqlite3_column_text(stmt, col);
    return text ? reinterpret_cast<const char*>(text) : std::string{};
}

}  // namespace

Database::Database(const std::string& db_path) : db_path_(db_path) {}

Database::~Database() {
    close();
}

bool Database::init() {
    std::lock_guard<std::mutex> lock(db_mtx_);

    if (db_ != nullptr) {
        return true;
    }

    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        if (db_ != nullptr) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    }

    // Wait up to 5 seconds if the database file is momentarily busy
    sqlite3_busy_timeout(db_, 5000);

    // Enable foreign keys
    sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);

    if (!create_schema(nullptr)) {
        sqlite3_close(db_);
        db_ = nullptr;
        return false;
    }

    return true;
}

void Database::close() {
    std::lock_guard<std::mutex> lock(db_mtx_);

    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::create_schema(std::string* err_msg) {
    const char* sql = R"SQL(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT NOT NULL UNIQUE,
            created_at TEXT NOT NULL DEFAULT (datetime('now'))
        );

        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            sender TEXT NOT NULL,
            recipient TEXT NOT NULL,
            body TEXT NOT NULL,
            timestamp TEXT NOT NULL DEFAULT (datetime('now')),
            is_read INTEGER NOT NULL DEFAULT 0
        );

        CREATE INDEX IF NOT EXISTS idx_messages_recipient_read
            ON messages(recipient, is_read);

        CREATE INDEX IF NOT EXISTS idx_messages_pair
            ON messages(sender, recipient, id);
    )SQL";

    char* raw_err = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &raw_err);
    if (rc != SQLITE_OK) {
        set_error(err_msg, raw_err ? raw_err : "Failed to initialize SQLite schema");
        if (raw_err != nullptr) {
            sqlite3_free(raw_err);
        }
        return false;
    }

    return true;
}

bool Database::authenticate_or_register_user(const std::string& username, std::string* err_msg) {
    std::lock_guard<std::mutex> lock(db_mtx_);

    if (db_ == nullptr) {
        set_error(err_msg, "Database is not initialized");
        return false;
    }
    if (username.empty()) {
        set_error(err_msg, "Username cannot be empty");
        return false;
    }

    const char* sql = "INSERT OR IGNORE INTO users (username) VALUES (?1);";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        set_error(err_msg, sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        set_error(err_msg, sqlite3_errmsg(db_));
        return false;
    }

    return true;
}

bool Database::store_message(const std::string& sender,
                             const std::string& recipient,
                             const std::string& body,
                             bool delivered_immediately,
                             std::string* err_msg) {
    std::lock_guard<std::mutex> lock(db_mtx_);

    if (db_ == nullptr) {
        set_error(err_msg, "Database is not initialized");
        return false;
    }

    const char* sql =
        "INSERT INTO messages (sender, recipient, body, is_read) VALUES (?1, ?2, ?3, ?4);";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        set_error(err_msg, sqlite3_errmsg(db_));
        return false;
    }

    const int is_read_val = delivered_immediately ? 1 : 0;
    sqlite3_bind_text(stmt, 1, sender.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, recipient.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, body.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, is_read_val);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        set_error(err_msg, sqlite3_errmsg(db_));
        return false;
    }

    return true;
}

std::vector<StoredMessage> Database::fetch_and_mark_unread(const std::string& username,
                                                             std::string* err_msg) {
    std::lock_guard<std::mutex> lock(db_mtx_);
    std::vector<StoredMessage> results;

    if (db_ == nullptr) {
        set_error(err_msg, "Database is not initialized");
        return results;
    }

    const char* select_sql =
        "SELECT id, sender, recipient, body, timestamp, is_read "
        "FROM messages "
        "WHERE recipient = ?1 AND is_read = 0 "
        "ORDER BY id ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, select_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        set_error(err_msg, sqlite3_errmsg(db_));
        return results;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    while (true) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            StoredMessage msg;
            msg.id = sqlite3_column_int64(stmt, 0);
            msg.sender = column_text(stmt, 1);
            msg.recipient = column_text(stmt, 2);
            msg.body = column_text(stmt, 3);
            msg.timestamp = column_text(stmt, 4);
            msg.is_read = (sqlite3_column_int(stmt, 5) != 0);
            results.push_back(std::move(msg));
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            set_error(err_msg, sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            results.clear();
            return results;
        }
    }

    sqlite3_finalize(stmt);

    if (!results.empty()) {
        const char* update_sql =
            "UPDATE messages SET is_read = 1 WHERE recipient = ?1 AND is_read = 0;";
        sqlite3_stmt* upd_stmt = nullptr;
        if (sqlite3_prepare_v2(db_, update_sql, -1, &upd_stmt, nullptr) != SQLITE_OK) {
            set_error(err_msg, sqlite3_errmsg(db_));
            return results;
        }
        sqlite3_bind_text(upd_stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(upd_stmt) != SQLITE_DONE) {
            set_error(err_msg, sqlite3_errmsg(db_));
        }
        sqlite3_finalize(upd_stmt);
    }

    return results;
}

std::vector<StoredMessage> Database::get_conversation_history(const std::string& user_a,
                                                              const std::string& user_b,
                                                              std::string* err_msg) {
    std::lock_guard<std::mutex> lock(db_mtx_);
    std::vector<StoredMessage> results;

    if (db_ == nullptr) {
        set_error(err_msg, "Database is not initialized");
        return results;
    }

    const char* sql =
        "SELECT id, sender, recipient, body, timestamp, is_read "
        "FROM messages "
        "WHERE (sender = ?1 AND recipient = ?2) OR (sender = ?2 AND recipient = ?1) "
        "ORDER BY id ASC;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        set_error(err_msg, sqlite3_errmsg(db_));
        return results;
    }

    sqlite3_bind_text(stmt, 1, user_a.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, user_b.c_str(), -1, SQLITE_TRANSIENT);

    while (true) {
        int rc = sqlite3_step(stmt);
        if (rc == SQLITE_ROW) {
            StoredMessage msg;
            msg.id = sqlite3_column_int64(stmt, 0);
            msg.sender = column_text(stmt, 1);
            msg.recipient = column_text(stmt, 2);
            msg.body = column_text(stmt, 3);
            msg.timestamp = column_text(stmt, 4);
            msg.is_read = (sqlite3_column_int(stmt, 5) != 0);
            results.push_back(std::move(msg));
        } else if (rc == SQLITE_DONE) {
            break;
        } else {
            set_error(err_msg, sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            results.clear();
            return results;
        }
    }

    sqlite3_finalize(stmt);
    return results;
}

bool Database::delete_conversation_history(const std::string& user_a,
                                           const std::string& user_b,
                                           std::string* err_msg) {
    std::lock_guard<std::mutex> lock(db_mtx_);

    if (db_ == nullptr) {
        set_error(err_msg, "Database is not initialized");
        return false;
    }

    const char* sql =
        "DELETE FROM messages "
        "WHERE (sender = ?1 AND recipient = ?2) OR (sender = ?2 AND recipient = ?1);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        set_error(err_msg, sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_text(stmt, 1, user_a.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, user_b.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        set_error(err_msg, sqlite3_errmsg(db_));
        return false;
    }

    return true;
}

}  // namespace chat
