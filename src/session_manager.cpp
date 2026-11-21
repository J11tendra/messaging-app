#include "session_manager.hpp"

namespace chat {

bool SessionManager::add_session(const std::string& username, int client_fd) {
    if (username.empty() || client_fd < 0) {
        return false;
    }

    std::lock_guard<std::mutex> lock(session_mtx_);

    // Prevent duplicate logins for already-active users
    if (user_to_fd_.find(username) != user_to_fd_.end()) {
        return false;
    }

    // Ensure a reused fd does not leave a stale entry
    auto stale_it = fd_to_user_.find(client_fd);
    if (stale_it != fd_to_user_.end()) {
        user_to_fd_.erase(stale_it->second);
        fd_to_user_.erase(stale_it);
    }

    user_to_fd_[username] = client_fd;
    fd_to_user_[client_fd] = username;
    return true;
}

std::optional<int> SessionManager::remove_by_username(const std::string& username) {
    std::lock_guard<std::mutex> lock(session_mtx_);

    auto it = user_to_fd_.find(username);
    if (it == user_to_fd_.end()) {
        return std::nullopt;
    }

    int fd = it->second;
    fd_to_user_.erase(fd);
    user_to_fd_.erase(it);
    return fd;
}

std::optional<std::string> SessionManager::remove_by_fd(int client_fd) {
    std::lock_guard<std::mutex> lock(session_mtx_);

    auto it = fd_to_user_.find(client_fd);
    if (it == fd_to_user_.end()) {
        return std::nullopt;
    }

    std::string username = it->second;
    user_to_fd_.erase(username);
    fd_to_user_.erase(it);
    return username;
}

std::optional<int> SessionManager::get_fd(const std::string& username) const {
    std::lock_guard<std::mutex> lock(session_mtx_);

    auto it = user_to_fd_.find(username);
    if (it == user_to_fd_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool SessionManager::is_online(const std::string& username) const {
    std::lock_guard<std::mutex> lock(session_mtx_);
    return user_to_fd_.find(username) != user_to_fd_.end();
}

std::vector<std::string> SessionManager::get_online_users() const {
    std::lock_guard<std::mutex> lock(session_mtx_);

    std::vector<std::string> users;
    users.reserve(user_to_fd_.size());
    for (const auto& entry : user_to_fd_) {
        users.push_back(entry.first);
    }
    return users;
}

std::vector<std::pair<std::string, int>> SessionManager::get_all_sessions() const {
    std::lock_guard<std::mutex> lock(session_mtx_);

    std::vector<std::pair<std::string, int>> sessions;
    sessions.reserve(user_to_fd_.size());
    for (const auto& entry : user_to_fd_) {
        sessions.emplace_back(entry.first, entry.second);
    }
    return sessions;
}

size_t SessionManager::active_count() const {
    std::lock_guard<std::mutex> lock(session_mtx_);
    return user_to_fd_.size();
}

}  // namespace chat
