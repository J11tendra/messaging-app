/**
 * @file session_manager.h
 * @brief Thread-safe table of online users (the server's "online user list").
 *
 * Synchronization design
 * ----------------------
 *  - SessionManager::mtx_      protects the username -> Session map.
 *  - Session::send_mtx_        one per socket. Several threads may write to the
 *                              same client (its own worker replying, another
 *                              worker forwarding a message, the main thread
 *                              broadcasting shutdown). The lock keeps each
 *                              1,096-byte packet contiguous on the stream.
 *  - delivery_lock(user)       one per user. Held while (a) a sender decides
 *                              "online -> push" vs "offline -> store unread",
 *                              and (b) the user logs in and drains unread
 *                              messages. This closes the race where a message
 *                              is stored as unread just after the login drain
 *                              and would otherwise not appear until the next login.
 *
 *  Lock order (to avoid deadlock): delivery_lock -> mtx_ -> send_mtx_.
 *  No code path acquires them in the opposite direction.
 */
#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "protocol.h"

namespace chat {

/** One logged-in client. Shared (shared_ptr) so a sender can still hold it safely
 *  while the owner disconnects; once closed, send() becomes a no-op so we never
 *  write to a file descriptor number that the OS may have reused. */
class Session {
public:
    Session(std::string username, int fd) : username_(std::move(username)), fd_(fd) {}

    const std::string &username() const { return username_; }
    int fd() const { return fd_; }

    /** Serialized write of one packet. Returns false if closed or the write failed. */
    bool send(const Packet &pkt);

    /** Marks the session closed; subsequent send() calls fail without touching the fd. */
    void mark_closed();

private:
    const std::string username_;
    const int fd_;
    std::mutex send_mtx_;
    bool closed_{false};  // guarded by send_mtx_
};

using SessionPtr = std::shared_ptr<Session>;

class SessionManager {
public:
    enum class AddResult { Added, AlreadyConnected, ServerFull };

    SessionManager() = default;
    SessionManager(const SessionManager &) = delete;
    SessionManager &operator=(const SessionManager &) = delete;

    /** Atomically checks for a duplicate and inserts. max_sessions = 0 -> unlimited. */
    AddResult add(const SessionPtr &session, size_t max_sessions = 0);

    /** Removes the entry only if it still refers to this exact session. */
    void remove(const SessionPtr &session);

    /** nullptr if the user is offline. */
    SessionPtr find(const std::string &username) const;

    bool is_online(const std::string &username) const { return find(username) != nullptr; }

    /** Sorted list of online user names. */
    std::vector<std::string> online_users() const;

    /** Snapshot of all sessions (used for the shutdown broadcast). */
    std::vector<SessionPtr> all() const;

    size_t count() const;

    /** Per-user lock used for the online/offline delivery decision (see file comment). */
    std::mutex &delivery_lock(const std::string &username);

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, SessionPtr> sessions_;

    std::mutex locks_mtx_;
    std::unordered_map<std::string, std::unique_ptr<std::mutex>> delivery_locks_;
};

}  // namespace chat

#endif  // SESSION_MANAGER_H
