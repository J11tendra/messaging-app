/**
 * @file receiver_thread.cpp
 * @brief Displays server packets as they arrive.
 */
#include "receiver_thread.h"

#include <unistd.h>

#include "console.h"
#include "network.h"
#include "protocol.h"

namespace chat {

namespace {
void wake_main(int fd) {
    const char c = 'R';
    ssize_t ignored = ::write(fd, &c, 1);
    (void)ignored;
}
}  // namespace

void run_receiver_thread(ReceiverContext ctx) {
    Packet pkt;
    while (recv_packet(ctx.sock_fd, pkt)) {
        switch (static_cast<ResponseType>(pkt.type)) {
            case ResponseType::MSG_REALTIME:
                // Format required by the brief.
                console_print("Message from " + pkt.get_sender() + ": " + pkt.get_payload());
                break;
            case ResponseType::RESP_UNREAD:
                console_print("[unread] Message from " + pkt.get_sender() + ": " + pkt.get_payload());
                break;
            case ResponseType::RESP_ERROR:
                console_print("[error] " + pkt.get_payload());
                break;
            case ResponseType::SERVER_SHUTDOWN:
                console_print(pkt.get_payload(), false);  // "Server shutting down..."
                ctx.server_gone->store(true);
                wake_main(ctx.wake_fd);
                return;
            case ResponseType::RESP_HISTORY:
            case ResponseType::RESP_USERLIST:
            case ResponseType::RESP_SUCCESS:
            case ResponseType::RESP_INFO:
                console_print(pkt.get_payload());
                break;
            default:
                break;
        }
    }
    // Socket closed without a shutdown broadcast.
    if (!ctx.quitting->load()) {
        console_print("[info] Connection to server lost.", false);
        ctx.server_gone->store(true);
    }
    wake_main(ctx.wake_fd);
}

}  // namespace chat
