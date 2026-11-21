#pragma once

#include <atomic>

namespace chat {

struct ReceiverContext {
    int sock_fd = -1;
    std::atomic<bool>* running = nullptr;
    std::atomic<bool>* connected = nullptr;
};

// Background thread entry point that reads packets from the server socket
// and prints incoming messages or server notifications asynchronously.
void run_receiver_thread(ReceiverContext ctx);

}  // namespace chat
