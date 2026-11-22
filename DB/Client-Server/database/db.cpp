#include "db.h"
#include <sqlite3.h>
#include <mutex>
#include <iostream>
#include <sstream>

static sqlite3 *g_db = nullptr;
static std::mutex g_db_mtx;

bool db_init(const std::string &db_path) {
    std::lock_guard<std::mutex> lock(g_db_mtx);

    if (g_db) {
        return true; // Already initialized
    }

    int rc = sqlite3_open(db_path.c_str(), &g_db);
    if (rc != SQLITE_OK) {
        std::cerr << "[Database] Failed to open " << db_path << ": " << sqlite3_errmsg(g_db) << "\n";
        g_db = nullptr;
        return false;
    }

    // Enable foreign keys
    char *err_msg = nullptr;
    sqlite3_exec(g_db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);

    // 1. Create users table
    const char *sql_users = 
        "CREATE TABLE IF NOT EXISTS users ("
        "  username TEXT PRIMARY KEY NOT NULL,"
        "  password TEXT DEFAULT 'password123'"
        ");";

    rc = sqlite3_exec(g_db, sql_users, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "[Database] Error creating users table: " << err_msg << "\n";
        sqlite3_free(err_msg);
        return false;
    }

    // 2. Create messages table
    const char *sql_messages = 
        "CREATE TABLE IF NOT EXISTS messages ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  sender TEXT NOT NULL,"
        "  recipient TEXT NOT NULL,"
        "  content TEXT NOT NULL,"
        "  timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  is_read INTEGER DEFAULT 0,"
        "  FOREIGN KEY(sender) REFERENCES users(username),"
        "  FOREIGN KEY(recipient) REFERENCES users(username)"
        ");";

    rc = sqlite3_exec(g_db, sql_messages, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::cerr << "[Database] Error creating messages table: " << err_msg << "\n";
        sqlite3_free(err_msg);
        return false;
    }

    // 3. Seed default valid users (alice, bob, charlie, david, eve)
    const char *sql_seed = 
        "INSERT OR IGNORE INTO users (username, password) VALUES "
        "('alice', 'pass123'),"
        "('bob', 'pass123'),"
        "('charlie', 'pass123'),"
        "('david', 'pass123'),"
        "('eve', 'pass123');";

    sqlite3_exec(g_db, sql_seed, nullptr, nullptr, nullptr);

    return true;
}

void db_close() {
    std::lock_guard<std::mutex> lock(g_db_mtx);
    if (g_db) {
        sqlite3_close(g_db);
        g_db = nullptr;
    }
}

bool db_is_valid_user(const std::string &username) {
    std::lock_guard<std::mutex> lock(g_db_mtx);
    if (!g_db) return false;

    const char *sql = "SELECT 1 FROM users WHERE username = ?;";
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    bool exists = (rc == SQLITE_ROW);

    sqlite3_finalize(stmt);
    return exists;
}

bool db_add_user(const std::string &username, const std::string &password) {
    std::lock_guard<std::mutex> lock(g_db_mtx);
    if (!g_db) return false;

    const char *sql = "INSERT OR IGNORE INTO users (username, password) VALUES (?, ?);";
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, password.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool db_save_message(const std::string &sender, const std::string &recipient, const std::string &content, bool is_read) {
    std::lock_guard<std::mutex> lock(g_db_mtx);
    if (!g_db) return false;

    const char *sql = "INSERT INTO messages (sender, recipient, content, is_read) VALUES (?, ?, ?, ?);";
    sqlite3_stmt *stmt = nullptr;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, sender.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, recipient.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, is_read ? 1 : 0);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

std::vector<Packet> db_get_unread_messages(const std::string &username) {
    std::lock_guard<std::mutex> lock(g_db_mtx);
    std::vector<Packet> unread_packets;
    if (!g_db) return unread_packets;

    // 1. Fetch unread messages
    const char *sql_select = 
        "SELECT sender, content, timestamp FROM messages "
        "WHERE recipient = ? AND is_read = 0 "
        "ORDER BY timestamp ASC, id ASC;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql_select, -1, &stmt, nullptr) != SQLITE_OK) {
        return unread_packets;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *sender_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        const char *content_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        const char *timestamp_text = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));

        std::string sender = sender_text ? sender_text : "";
        std::string content = content_text ? content_text : "";
        std::string ts = timestamp_text ? timestamp_text : "";

        std::string payload = "[" + ts + "] " + content;

        Packet pkt = create_packet(
            static_cast<uint16_t>(ResponseType::RESP_UNREAD),
            sender,
            username,
            payload
        );
        unread_packets.push_back(pkt);
    }
    sqlite3_finalize(stmt);

    // 2. Mark them as read in place
    const char *sql_update = "UPDATE messages SET is_read = 1 WHERE recipient = ? AND is_read = 0;";
    if (sqlite3_prepare_v2(g_db, sql_update, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    return unread_packets;
}

std::string db_get_conversation(const std::string &user1, const std::string &user2) {
    std::lock_guard<std::mutex> lock(g_db_mtx);
    if (!g_db) return "Database not initialized.\n";

    const char *sql = 
        "SELECT sender, content, timestamp FROM messages "
        "WHERE (sender = ? AND recipient = ?) OR (sender = ? AND recipient = ?) "
        "ORDER BY timestamp ASC, id ASC;";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return "Failed to query message history.\n";
    }

    sqlite3_bind_text(stmt, 1, user1.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user2.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, user2.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, user1.c_str(), -1, SQLITE_STATIC);

    std::stringstream ss;
    bool has_messages = false;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        has_messages = true;
        const char *sender = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        const char *content = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        const char *ts = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));

        ss << "[" << (ts ? ts : "") << "] " 
           << (sender ? sender : "") << ": " 
           << (content ? content : "") << "\n";
    }

    sqlite3_finalize(stmt);

    if (!has_messages) {
        return "No message history exchanged with " + user2 + ".\n";
    }

    return ss.str();
}

bool db_delete_conversation(const std::string &user1, const std::string &user2) {
    std::lock_guard<std::mutex> lock(g_db_mtx);
    if (!g_db) return false;

    const char *sql = 
        "DELETE FROM messages "
        "WHERE (sender = ? AND recipient = ?) OR (sender = ? AND recipient = ?);";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, user1.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, user2.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, user2.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, user1.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}
