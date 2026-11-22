#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <utility>

namespace chat {

class SessionManager {
public:
    SessionManager() = default;
    ~SessionManager() = default;

    // Non-copyable and non-movable
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;
    SessionManager(SessionManager&&) = delete;
    SessionManager& operator=(SessionManager&&) = delete;

    // Registers an active session; returns false if username is already connected (duplicate prevention)
    bool add_session(const std::string &username, int client_fd);

    // Removes an active session by username
    void remove_by_username(const std::string &username);

    // Removes an active session by socket fd; returns username if found
    std::string remove_by_fd(int client_fd);

    // Retrieves socket fd for username (-1 if offline)
    int get_socket(const std::string &username) const;

    // Checks if username currently has an active session
    bool is_online(const std::string &username) const;

    // Returns a list of all currently connected usernames
    std::vector<std::string> get_online_users() const;

    // Returns all active (username, socket_fd) pairs for shutdown broadcast
    std::vector<std::pair<std::string, int>> get_all_sessions() const;

    // Returns count of active sessions
    size_t active_count() const;

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, int> user_to_fd_;
    std::unordered_map<int, std::string> fd_to_user_;
};

}  // namespace chat

#endif // SESSION_MANAGER_H
