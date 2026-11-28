/**
 * @file session_manager.cpp
 * @brief Online-user table and per-socket send serialization.
 */
#include "session_manager.h"

#include <algorithm>

#include "network.h"

namespace chat {

bool Session::send(const Packet &pkt) {
    std::lock_guard<std::mutex> lock(send_mtx_);
    if (closed_) return false;
    return send_packet(fd_, pkt);
}

void Session::mark_closed() {
    std::lock_guard<std::mutex> lock(send_mtx_);
    closed_ = true;
}

SessionManager::AddResult SessionManager::add(const SessionPtr &session, size_t max_sessions) {
    std::lock_guard<std::mutex> lock(mtx_);
    // Check-and-insert happen under the same lock: two simultaneous logins with
    // the same name cannot both succeed.
    if (sessions_.count(session->username())) return AddResult::AlreadyConnected;
    if (max_sessions > 0 && sessions_.size() >= max_sessions) return AddResult::ServerFull;
    sessions_.emplace(session->username(), session);
    return AddResult::Added;
}

void SessionManager::remove(const SessionPtr &session) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(session->username());
    if (it != sessions_.end() && it->second == session) sessions_.erase(it);
}

SessionPtr SessionManager::find(const std::string &username) const {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(username);
    return it == sessions_.end() ? nullptr : it->second;
}

std::vector<std::string> SessionManager::online_users() const {
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lock(mtx_);
        names.reserve(sessions_.size());
        for (const auto &kv : sessions_) names.push_back(kv.first);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<SessionPtr> SessionManager::all() const {
    std::lock_guard<std::mutex> lock(mtx_);
    std::vector<SessionPtr> out;
    out.reserve(sessions_.size());
    for (const auto &kv : sessions_) out.push_back(kv.second);
    return out;
}

size_t SessionManager::count() const {
    std::lock_guard<std::mutex> lock(mtx_);
    return sessions_.size();
}

std::mutex &SessionManager::delivery_lock(const std::string &username) {
    std::lock_guard<std::mutex> lock(locks_mtx_);
    auto &slot = delivery_locks_[username];
    if (!slot) slot = std::make_unique<std::mutex>();
    return *slot;  // stable address: entries are never erased
}

}  // namespace chat
