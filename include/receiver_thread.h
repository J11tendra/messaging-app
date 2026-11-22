#ifndef RECEIVER_THREAD_H
#define RECEIVER_THREAD_H

#include <atomic>

namespace chat {

struct ReceiverContext {
    int sock_fd{-1};
    std::atomic<bool> *running{nullptr};
    std::atomic<bool> *connected{nullptr};
};

// Dual-thread client receiver loop: continuously receives asynchronous incoming
// messages and server notifications while user interacts with the CLI.
void run_receiver_thread(ReceiverContext ctx);

}  // namespace chat

#endif // RECEIVER_THREAD_H
