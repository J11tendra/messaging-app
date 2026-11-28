/**
 * @file connection_tracker.h
 * @brief Tracks every accepted socket and its worker thread so the server can
 *        unblock and join all workers at shutdown (no detached threads).
 */
#ifndef CONNECTION_TRACKER_H
#define CONNECTION_TRACKER_H

#include <atomic>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace chat {

class ConnectionTracker {
public:
    /** Records an accepted fd. Must be called before the worker starts. */
    void add_fd(int fd);

    /** Called by a worker when it is done: closes and forgets its fd. */
    void close_fd(int fd);

    /** Shutdown: wakes every worker blocked in recv() by shutting its socket down. */
    void shutdown_all();

    /** Starts a worker thread; `done` is set by the thread when it returns. */
    void start_worker(std::thread thread, std::shared_ptr<std::atomic<bool>> done);

    /** Joins workers that have already finished (called periodically). */
    void reap_finished();

    /** Joins every worker (blocks). */
    void join_all();

    size_t open_connections() const;

private:
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

    mutable std::mutex fds_mtx_;  // guards fds_, and makes close vs shutdown_all exclusive
    std::set<int> fds_;

    std::mutex workers_mtx_;
    std::vector<Worker> workers_;
};

}  // namespace chat

#endif  // CONNECTION_TRACKER_H
