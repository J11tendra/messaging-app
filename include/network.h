/**
 * @file network.h
 * @brief Socket (IPC) layer: framing of Packets over TCP and socket setup.
 */
#ifndef NETWORK_H
#define NETWORK_H

#include <cstdint>
#include <string>

#include "protocol.h"

namespace chat {

/**
 * Writes one whole Packet. Loops over partial writes and EINTR.
 * Uses MSG_NOSIGNAL so a dead peer gives an error instead of SIGPIPE.
 * NOT thread-safe on its own: callers sharing a socket must serialize
 * (the server does this with Session::send()).
 */
bool send_packet(int socket_fd, const Packet &pkt);

/**
 * Reads exactly one Packet (blocking). Returns false on EOF, error, or a
 * malformed frame (payload_len out of range).
 */
bool recv_packet(int socket_fd, Packet &pkt);

/** socket()+bind()+listen() on all interfaces. Returns fd or -1 (err filled). */
int create_listen_socket(uint16_t port, std::string &err);

/** socket()+connect() to an IPv4 address. Returns fd or -1 (err filled). */
int connect_to_server(const std::string &ip, uint16_t port, std::string &err);

/** Sets/clears a receive timeout (seconds; 0 = block forever). */
void set_recv_timeout(int fd, int seconds);

/** shutdown(SHUT_RDWR) + close(); ignores fd < 0. */
void close_socket(int fd);

}  // namespace chat

#endif  // NETWORK_H
