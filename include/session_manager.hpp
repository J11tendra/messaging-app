#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace chat {

class SessionManager {
public:
    SessionManager() = default;
    ~SessionManager() = default;

    // Non-copyable and non-movable due to internal std::mutex
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    SessionManager(SessionManager&&) = delete;
    SessionManager& operator=(SessionManager&&) = delete;

    // Registers an active session for `username` with socket descriptor `client_fd`.
    // Returns false if `username` is already logged in or invalid.
    bool add_session(const std::string& username, int client_fd);

    // Removes session by username; returns the associated socket fd if found.
    std::optional<int> remove_by_username(const std::string& username);

    // Removes session by socket descriptor; returns the associated username if found.
    std::optional<std::string> remove_by_fd(int client_fd);

    // Looks up active socket fd for real-time routing to `username`.
    std::optional<int> get_fd(const std::string& username) const;

    // Checks whether `username` currently has an active session.
    bool is_online(const std::string& username) const;

    // Returns a snapshot of all currently online usernames.
    std::vector<std::string> get_online_users() const;

    // Returns a snapshot of all active (username, client_fd) pairs for shutdown broadcasts.
    std::vector<std::pair<std::string, int>> get_all_sessions() const;

    // Returns current number of active sessions.
    size_t active_count() const;

private:
    mutable std::mutex session_mtx_;
    std::unordered_map<std::string, int> user_to_fd_;
    std::unordered_map<int, std::string> fd_to_user_;
};

}  // namespace chat
