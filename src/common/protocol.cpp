/**
 * @file protocol.cpp
 * @brief Packet field helpers and packet factories.
 */
#include "protocol.h"

#include <algorithm>
#include <cstring>

namespace chat {

namespace {

// Copies at most (cap - 1) bytes and always '\0'-terminates; returns bytes copied.
size_t copy_field(char *dst, size_t cap, const std::string &src) {
    std::memset(dst, 0, cap);
    const size_t len = std::min(src.size(), cap - 1);
    std::memcpy(dst, src.data(), len);
    return len;
}

// Reads a fixed buffer that may not be terminated (defensive).
std::string read_field(const char *src, size_t cap) {
    return std::string(src, strnlen(src, cap));
}

}  // namespace

void Packet::set_sender(const std::string &name)    { copy_field(sender, MAX_USERNAME_LEN, name); }
void Packet::set_recipient(const std::string &name) { copy_field(recipient, MAX_USERNAME_LEN, name); }
void Packet::set_payload(const std::string &text) {
    payload_len = static_cast<uint32_t>(copy_field(payload, MAX_PAYLOAD_LEN, text));
}

std::string Packet::get_sender() const    { return read_field(sender, MAX_USERNAME_LEN); }
std::string Packet::get_recipient() const { return read_field(recipient, MAX_USERNAME_LEN); }
std::string Packet::get_payload() const   { return read_field(payload, MAX_PAYLOAD_LEN); }

Packet create_packet(uint16_t type, const std::string &sender, const std::string &recipient,
                     const std::string &payload, StatusCode status) {
    Packet pkt;
    pkt.type = type;
    pkt.status = static_cast<uint16_t>(status);
    pkt.set_sender(sender);
    pkt.set_recipient(recipient);
    pkt.set_payload(payload);
    return pkt;
}

Packet create_packet(CommandType type, const std::string &sender,
                     const std::string &recipient, const std::string &payload) {
    return create_packet(static_cast<uint16_t>(type), sender, recipient, payload);
}

Packet create_packet(ResponseType type, const std::string &sender,
                     const std::string &recipient, const std::string &payload) {
    return create_packet(static_cast<uint16_t>(type), sender, recipient, payload);
}

Packet create_error_packet(StatusCode code, const std::string &error_msg) {
    return create_packet(static_cast<uint16_t>(ResponseType::RESP_ERROR), "server", "",
                         error_msg, code);
}

}  // namespace chat
