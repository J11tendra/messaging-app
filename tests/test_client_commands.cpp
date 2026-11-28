/**
 * Unit tests: client CLI parsing and request construction.
 */
#include "client_commands.h"
#include "test_framework.h"

using namespace chat;

TEST_CASE(connect_parsing) {
    auto c = parse_command("connect 127.0.0.1 Rouchelle");
    CHECK(c.kind == ClientCommand::Connect);
    CHECK_EQ(c.arg1, "127.0.0.1");
    CHECK_EQ(c.arg2, "Rouchelle");
    CHECK(parse_command("connect 127.0.0.1").kind == ClientCommand::Invalid);
    CHECK(parse_command("connect a b c").kind == ClientCommand::Invalid);
    Packet p;
    CHECK(build_request(c, "", p));
    CHECK_EQ(p.type, static_cast<uint16_t>(CommandType::CMD_CONNECT));
    CHECK_EQ(p.get_sender(), "Rouchelle");
}

TEST_CASE(sendmessage_keeps_spaces_in_text) {
    auto c = parse_command("  sendmessage Tanvi   hello there,  Tanvi!  ");
    CHECK(c.kind == ClientCommand::SendMessage);
    CHECK_EQ(c.arg1, "Tanvi");
    CHECK_EQ(c.arg2, "hello there,  Tanvi!");
    Packet p;
    CHECK(build_request(c, "Rouchelle", p));
    CHECK_EQ(p.get_sender(), "Rouchelle");
    CHECK_EQ(p.get_recipient(), "Tanvi");
    CHECK_EQ(p.get_payload(), "hello there,  Tanvi!");
}

TEST_CASE(sendmessage_validation) {
    CHECK(parse_command("sendmessage Tanvi").kind == ClientCommand::Invalid);
    CHECK(parse_command("sendmessage").kind == ClientCommand::Invalid);
    auto too_long = parse_command("sendmessage Tanvi " + std::string(MAX_MESSAGE_TEXT + 1, 'x'));
    CHECK(too_long.kind == ClientCommand::Invalid);
    CHECK(parse_command("sendmessage Tanvi " + std::string(MAX_MESSAGE_TEXT, 'x')).kind ==
          ClientCommand::SendMessage);
}

TEST_CASE(history_commands) {
    CHECK(parse_command("getmessages Tanvi").kind == ClientCommand::GetMessages);
    CHECK(parse_command("deletemessages Tanvi").kind == ClientCommand::DeleteMessages);
    CHECK(parse_command("getmessages").kind == ClientCommand::Invalid);
    CHECK(parse_command("deletemessages a b").kind == ClientCommand::Invalid);
    Packet p;
    CHECK(build_request(parse_command("deletemessages Tanvi"), "Rouchelle", p));
    CHECK_EQ(p.type, static_cast<uint16_t>(CommandType::CMD_DEL_MSGS));
    CHECK_EQ(p.get_recipient(), "Tanvi");
}

TEST_CASE(other_commands) {
    CHECK(parse_command("getuserlist").kind == ClientCommand::GetUserList);
    CHECK(parse_command("shutdown").kind == ClientCommand::Shutdown);
    CHECK(parse_command("help").kind == ClientCommand::Help);
    CHECK(parse_command("   ").kind == ClientCommand::Empty);
    CHECK(parse_command("dance").kind == ClientCommand::Unknown);
    Packet p;
    CHECK(!build_request(parse_command("help"), "Rouchelle", p));  // local only
}

int main() { return test::run_all("client commands"); }
