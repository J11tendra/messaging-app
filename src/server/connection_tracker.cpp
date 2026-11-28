/**
 * @file connection_tracker.cpp
 */
#include "connection_tracker.h"

#include <sys/socket.h>
#include <unistd.h>

namespace chat {

void ConnectionTracker::add_fd(int fd) {
    std::lock_guard<std::mutex> lock(fds_mtx_);
    fds_.insert(fd);
}

void ConnectionTracker::close_fd(int fd) {
    // Closing under the lock guarantees shutdown_all() never touches an fd
    // number after it has been released to the OS (and possibly reused).
    std::lock_guard<std::mutex> lock(fds_mtx_);
    if (fds_.erase(fd)) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
}

void ConnectionTracker::shutdown_all() {
    std::lock_guard<std::mutex> lock(fds_mtx_);
    for (int fd : fds_) ::shutdown(fd, SHUT_RDWR);  // the owning worker closes it
}

void ConnectionTracker::start_worker(std::thread thread, std::shared_ptr<std::atomic<bool>> done) {
    std::lock_guard<std::mutex> lock(workers_mtx_);
    workers_.push_back(Worker{std::move(thread), std::move(done)});
}

void ConnectionTracker::reap_finished() {
    std::lock_guard<std::mutex> lock(workers_mtx_);
    for (auto it = workers_.begin(); it != workers_.end();) {
        if (it->done->load()) {
            if (it->thread.joinable()) it->thread.join();
            it = workers_.erase(it);
        } else {
            ++it;
        }
    }
}

void ConnectionTracker::join_all() {
    std::vector<Worker> to_join;
    {
        std::lock_guard<std::mutex> lock(workers_mtx_);
        to_join.swap(workers_);
    }
    for (auto &w : to_join) {
        if (w.thread.joinable()) w.thread.join();
    }
}

size_t ConnectionTracker::open_connections() const {
    std::lock_guard<std::mutex> lock(fds_mtx_);
    return fds_.size();
}

}  // namespace chat
