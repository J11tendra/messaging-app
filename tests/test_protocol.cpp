/**
 * Unit tests: packet helpers and framing over a real socket pair.
 */
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <thread>

#include "network.h"
#include "protocol.h"
#include "test_framework.h"

using namespace chat;

TEST_CASE(fields_are_truncated_and_terminated) {
    Packet p;
    p.set_sender(std::string(100, 'a'));
    p.set_payload(std::string(5000, 'x'));
    CHECK_EQ(p.get_sender().size(), MAX_USERNAME_LEN - 1);
    CHECK_EQ(p.get_payload().size(), MAX_PAYLOAD_LEN - 1);
    CHECK_EQ(p.payload_len, MAX_PAYLOAD_LEN - 1);
}

TEST_CASE(round_trip_over_socketpair) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    Packet out = create_packet(CommandType::CMD_SEND_MSG, "Rouchelle", "Tanvi", "hello Tanvi");
    CHECK(send_packet(sv[0], out));
    Packet in;
    CHECK(recv_packet(sv[1], in));
    CHECK_EQ(in.type, static_cast<uint16_t>(CommandType::CMD_SEND_MSG));
    CHECK_EQ(in.get_sender(), "Rouchelle");
    CHECK_EQ(in.get_recipient(), "Tanvi");
    CHECK_EQ(in.get_payload(), "hello Tanvi");
    CHECK_EQ(in.payload_len, 11u);
    ::close(sv[0]);
    ::close(sv[1]);
}

TEST_CASE(integers_use_network_byte_order_on_the_wire) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    CHECK(send_packet(sv[0], create_packet(ResponseType::RESP_ERROR)));  // 101 = 0x0065
    unsigned char raw[sizeof(Packet)];
    CHECK_EQ(::recv(sv[1], raw, sizeof(raw), MSG_WAITALL), static_cast<ssize_t>(sizeof(Packet)));
    CHECK_EQ(raw[0], 0x00);
    CHECK_EQ(raw[1], 0x65);
    ::close(sv[0]);
    ::close(sv[1]);
}

TEST_CASE(packet_split_across_partial_writes_is_reassembled) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    Packet out = create_packet(CommandType::CMD_GET_MSGS, "Jitendra", "Akshay");
    Packet wire = out;
    wire.type = htons(out.type);
    wire.status = htons(out.status);
    wire.payload_len = htonl(out.payload_len);
    const char *bytes = reinterpret_cast<const char *>(&wire);
    std::thread writer([&] {
        for (size_t off = 0; off < sizeof(Packet); off += 100) {
            ::send(sv[0], bytes + off, std::min<size_t>(100, sizeof(Packet) - off), 0);
        }
    });
    Packet in;
    CHECK(recv_packet(sv[1], in));
    writer.join();
    CHECK_EQ(in.get_recipient(), "Akshay");
    ::close(sv[0]);
    ::close(sv[1]);
}

TEST_CASE(eof_and_malformed_length_are_rejected) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    Packet bad;
    bad.payload_len = htonl(5000);  // larger than the buffer
    CHECK_EQ(::send(sv[0], &bad, sizeof(bad), 0), static_cast<ssize_t>(sizeof(bad)));
    Packet in;
    CHECK(!recv_packet(sv[1], in));
    ::close(sv[0]);
    CHECK(!recv_packet(sv[1], in));  // EOF
    ::close(sv[1]);
}

int main() { return test::run_all("protocol / network"); }
