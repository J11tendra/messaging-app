/**
 * @file receiver_thread.h
 * @brief Client background thread: displays incoming packets in real time
 *        while the main thread keeps reading user commands.
 */
#ifndef RECEIVER_THREAD_H
#define RECEIVER_THREAD_H

#include <atomic>

namespace chat {

struct ReceiverContext {
    int sock_fd{-1};
    std::atomic<bool> *server_gone{nullptr};  // set when server shut down / connection lost
    std::atomic<bool> *quitting{nullptr};     // set by main thread during our own shutdown
    int wake_fd{-1};                          // pipe write end: tells main thread we stopped
};

/** Runs until the socket closes or the server broadcasts shutdown. */
void run_receiver_thread(ReceiverContext ctx);

}  // namespace chat

#endif  // RECEIVER_THREAD_H
