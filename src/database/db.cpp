/**
 * @file db.cpp
 * @brief SQLite implementation of the server message store.
 */
#include "db.h"

#include <sqlite3.h>
#include <sys/stat.h>

#include <iostream>
#include <mutex>

namespace chat {

namespace {

sqlite3 *g_db = nullptr;
std::mutex g_db_mtx;  // serializes every use of g_db

/** RAII wrapper so every prepared statement is finalized on all paths. */
class Statement {
public:
    Statement(sqlite3 *db, const char *sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) stmt_ = nullptr;
    }
    ~Statement() { if (stmt_) sqlite3_finalize(stmt_); }
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;

    bool ok() const { return stmt_ != nullptr; }
    sqlite3_stmt *get() const { return stmt_; }
    void bind(int idx, const std::string &v) {
        sqlite3_bind_text(stmt_, idx, v.c_str(), -1, SQLITE_TRANSIENT);
    }
    void bind(int idx, int64_t v) { sqlite3_bind_int64(stmt_, idx, v); }

private:
    sqlite3_stmt *stmt_{nullptr};
};

std::string column_text(sqlite3_stmt *stmt, int col) {
    const unsigned char *t = sqlite3_column_text(stmt, col);
    return t ? reinterpret_cast<const char *>(t) : "";
}

StoredMessage row_to_message(sqlite3_stmt *stmt) {
    StoredMessage m;
    m.id = sqlite3_column_int64(stmt, 0);
    m.sender = column_text(stmt, 1);
    m.recipient = column_text(stmt, 2);
    m.content = column_text(stmt, 3);
    m.timestamp = column_text(stmt, 4);
    return m;
}

bool exec(const char *sql) {
    char *err = nullptr;
    if (sqlite3_exec(g_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::cerr << "[database] " << (err ? err : "unknown error") << "\n";
        sqlite3_free(err);
        return false;
    }
    return true;
}

void ensure_parent_dir(const std::string &path) {
    size_t slash = path.find_last_of('/');
    if (slash != std::string::npos && slash > 0) {
        ::mkdir(path.substr(0, slash).c_str(), 0755);
    }
}

}  // namespace

bool db_init(const std::string &db_path) {
    std::lock_guard<std::mutex> lock(g_db_mtx);
    if (g_db) return true;

    ensure_parent_dir(db_path);
    if (sqlite3_open(db_path.c_str(), &g_db) != SQLITE_OK) {
        std::cerr << "[database] cannot open " << db_path << ": "
                  << (g_db ? sqlite3_errmsg(g_db) : "unknown error") << "\n";
        sqlite3_close(g_db);
        g_db = nullptr;
        return false;
    }
    sqlite3_busy_timeout(g_db, 5000);

    const bool ok =
        exec("PRAGMA foreign_keys = ON;") &&
        exec("CREATE TABLE IF NOT EXISTS users ("
             "  username TEXT PRIMARY KEY NOT NULL);") &&
        exec("CREATE TABLE IF NOT EXISTS messages ("
             "  id        INTEGER PRIMARY KEY AUTOINCREMENT,"
             "  sender    TEXT NOT NULL REFERENCES users(username),"
             "  recipient TEXT NOT NULL REFERENCES users(username),"
             "  content   TEXT NOT NULL,"
             "  timestamp DATETIME NOT NULL,"
             "  is_read   INTEGER NOT NULL DEFAULT 0);") &&
        exec("CREATE INDEX IF NOT EXISTS idx_unread ON messages(recipient, is_read);") &&
        exec("CREATE INDEX IF NOT EXISTS idx_pair ON messages(sender, recipient, id);");

    if (!ok) {
        sqlite3_close(g_db);
        g_db = nullptr;
    }
    return ok;
}

void db_close() {
    std::lock_guard<std::mutex> lock(g_db_mtx);
    if (g_db) {
        sqlite3_close(g_db);
        g_db = nullptr;
    }
}

bool db_add_user(const std::string &username) {
    std::lock_guard<std::mutex> lock(g_db_mtx);
    if (!g_db || username.empty()) return false;

    Statement st(g_db, "INSERT OR IGNORE INTO users (username) VALUES (?);");
    if (!st.ok()) return false;
    st.bind(1, username);
    return sqlite3_step(st.get()) == SQLITE_DONE;
}

bool db_save_message(const std::string &sender, const std::string &recipient,
                     const std::string &content, bool delivered) {
    std::lock_guard<std::mutex> lock(g_db_mtx);
    if (!g_db) return false;

    Statement st(g_db,
                 "INSERT INTO messages (sender, recipient, content, timestamp, is_read) "
                 "VALUES (?, ?, ?, datetime('now','localtime'), ?);");
    if (!st.ok()) return false;
    st.bind(1, sender);
    st.bind(2, recipient);
    st.bind(3, content);
    st.bind(4, static_cast<int64_t>(delivered ? 1 : 0));
    return sqlite3_step(st.get()) == SQLITE_DONE;
}

std::vector<StoredMessage> db_get_unread_messages(const std::string &username) {
    std::lock_guard<std::mutex> lock(g_db_mtx);
    std::vector<StoredMessage> out;
    if (!g_db) return out;

    Statement st(g_db,
                 "SELECT id, sender, recipient, content, timestamp FROM messages "
                 "WHERE recipient = ? AND is_read = 0 ORDER BY id ASC;");
    if (!st.ok()) return out;
    st.bind(1, username);
    while (sqlite3_step(st.get()) == SQLITE_ROW) out.push_back(row_to_message(st.get()));
    return out;
}

bool db_mark_read(const std::vector<int64_t> &ids) {
    std::lock_guard<std::mutex> lock(g_db_mtx);
    if (!g_db) return false;
    if (ids.empty()) return true;

    if (!exec("BEGIN;")) return false;
    bool ok = true;
    {
        Statement st(g_db, "UPDATE messages SET is_read = 1 WHERE id = ?;");
        ok = st.ok();
        for (size_t i = 0; ok && i < ids.size(); ++i) {
            st.bind(1, ids[i]);
            ok = sqlite3_step(st.get()) == SQLITE_DONE;
            sqlite3_reset(st.get());
        }
    }
    exec(ok ? "COMMIT;" : "ROLLBACK;");
    return ok;
}

std::vector<StoredMessage> db_get_conversation(const std::string &user1, const std::string &user2) {
    std::lock_guard<std::mutex> lock(g_db_mtx);
    std::vector<StoredMessage> out;
    if (!g_db) return out;

    Statement st(g_db,
                 "SELECT id, sender, recipient, content, timestamp FROM messages "
                 "WHERE (sender = ?1 AND recipient = ?2) OR (sender = ?2 AND recipient = ?1) "
                 "ORDER BY id ASC;");
    if (!st.ok()) return out;
    st.bind(1, user1);
    st.bind(2, user2);
    while (sqlite3_step(st.get()) == SQLITE_ROW) out.push_back(row_to_message(st.get()));
    return out;
}

int db_delete_conversation(const std::string &user1, const std::string &user2) {
    std::lock_guard<std::mutex> lock(g_db_mtx);
    if (!g_db) return -1;

    Statement st(g_db,
                 "DELETE FROM messages "
                 "WHERE (sender = ?1 AND recipient = ?2) OR (sender = ?2 AND recipient = ?1);");
    if (!st.ok()) return -1;
    st.bind(1, user1);
    st.bind(2, user2);
    if (sqlite3_step(st.get()) != SQLITE_DONE) return -1;
    return sqlite3_changes(g_db);
}

}  // namespace chat
