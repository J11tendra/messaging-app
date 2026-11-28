/**
 * Unit tests: SQLite message store (fresh temporary database per run).
 */
#include <unistd.h>

#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "db.h"
#include "test_framework.h"

using namespace chat;

static const std::string kDb = "/tmp/msgapp_test_db_" + std::to_string(::getpid()) + ".db";

TEST_CASE(init_and_users) {
    std::remove(kDb.c_str());
    CHECK(db_init(kDb));
    CHECK(db_add_user("Rouchelle"));
    CHECK(db_add_user("Tanvi"));
    CHECK(db_add_user("Jitendra"));
    CHECK(db_add_user("Rouchelle"));  // idempotent
}

TEST_CASE(message_to_unknown_user_is_refused_by_foreign_key) {
    CHECK(!db_save_message("Rouchelle", "mallory", "hi", false));
}

TEST_CASE(offline_messages_are_unread_until_marked) {
    CHECK(db_save_message("Rouchelle", "Tanvi", "live message", true));
    CHECK(db_save_message("Rouchelle", "Tanvi", "offline 1", false));
    CHECK(db_save_message("Jitendra", "Tanvi", "offline 2", false));

    auto unread = db_get_unread_messages("Tanvi");
    CHECK_EQ(unread.size(), 2u);
    if (unread.size() == 2) {
        CHECK_EQ(unread[0].content, "offline 1");  // oldest first
        CHECK_EQ(unread[1].sender, "Jitendra");
        CHECK(!unread[0].timestamp.empty());
    }
    // Not marked yet -> still unread (e.g. if delivery failed).
    CHECK_EQ(db_get_unread_messages("Tanvi").size(), 2u);

    std::vector<int64_t> ids;
    for (auto &m : unread) ids.push_back(m.id);
    CHECK(db_mark_read(ids));
    CHECK(db_get_unread_messages("Tanvi").empty());
}

TEST_CASE(conversation_contains_both_directions_only) {
    CHECK(db_save_message("Tanvi", "Rouchelle", "reply", true));
    auto conv = db_get_conversation("Rouchelle", "Tanvi");
    CHECK_EQ(conv.size(), 3u);  // live, offline 1, reply
    for (auto &m : conv) {
        CHECK((m.sender == "Rouchelle" && m.recipient == "Tanvi") ||
              (m.sender == "Tanvi" && m.recipient == "Rouchelle"));
    }
    CHECK_EQ(db_get_conversation("Tanvi", "Rouchelle").size(), 3u);  // symmetric
}

TEST_CASE(delete_removes_pair_only) {
    CHECK_EQ(db_delete_conversation("Tanvi", "Rouchelle"), 3);
    CHECK(db_get_conversation("Rouchelle", "Tanvi").empty());
    CHECK_EQ(db_get_conversation("Jitendra", "Tanvi").size(), 1u);  // untouched
    CHECK_EQ(db_delete_conversation("Rouchelle", "Tanvi"), 0);
}

TEST_CASE(concurrent_writers_are_serialized) {
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([t] {
            for (int i = 0; i < 25; ++i) db_save_message("Rouchelle", "Jitendra", "t" + std::to_string(t), true);
        });
    }
    for (auto &th : threads) th.join();
    CHECK_EQ(db_get_conversation("Rouchelle", "Jitendra").size(), 200u);
}

TEST_CASE(persistence_across_reopen) {
    db_close();
    CHECK(db_init(kDb));
    CHECK_EQ(db_get_conversation("Rouchelle", "Jitendra").size(), 200u);
    db_close();
    std::remove(kDb.c_str());
}

int main() { return test::run_all("database"); }
