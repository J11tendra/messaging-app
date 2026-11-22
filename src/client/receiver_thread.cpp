#include "receiver_thread.h"

#include <iostream>
#include <string>
#include <cstdlib>

#include "protocol.h"

namespace chat {

void run_receiver_thread(ReceiverContext ctx) {
    const int sock_fd = ctx.sock_fd;
    std::atomic<bool> *running = ctx.running;
    std::atomic<bool> *connected = ctx.connected;

    Packet pkt;

    while ((!running || running->load()) && (!connected || connected->load())) {
        if (!recv_packet(sock_fd, pkt)) {
            if (connected && connected->load()) {
                std::cout << "\n[info] Server connection closed.\n> " << std::flush;
                connected->store(false);
            }
            break;
        }

        const auto type = static_cast<ResponseType>(pkt.type);

        if (type == ResponseType::SERVER_SHUTDOWN) {
            std::cout << "\nServer shutting down...\n" << std::flush;
            if (connected) connected->store(false);
            if (running) running->store(false);
            std::exit(0);
        }

        switch (type) {
            case ResponseType::MSG_REALTIME: {
                // Real-time message format required by project doc:
                // "Message from <sender>: <message text>"
                std::cout << "\nMessage from " << pkt.get_sender() << ": "
                          << pkt.get_payload() << "\n> " << std::flush;
                break;
            }

            case ResponseType::RESP_UNREAD: {
                // Offline unread message delivered at login
                std::cout << "\n[Unread] Message from " << pkt.get_sender() << ": "
                          << pkt.get_payload() << "\n> " << std::flush;
                break;
            }

            case ResponseType::RESP_HISTORY: {
                std::cout << "\n" << pkt.get_payload() << "> " << std::flush;
                break;
            }

            case ResponseType::RESP_USERLIST: {
                std::cout << "\n" << pkt.get_payload() << "\n> " << std::flush;
                break;
            }

            case ResponseType::RESP_ERROR: {
                std::cout << "\n[Error] " << pkt.get_payload() << "\n> " << std::flush;
                break;
            }

            case ResponseType::RESP_SUCCESS: {
                std::cout << "\n" << pkt.get_payload() << "\n> " << std::flush;
                break;
            }

            default:
                break;
        }
    }

    if (connected) {
        connected->store(false);
    }
}

}  // namespace chat
