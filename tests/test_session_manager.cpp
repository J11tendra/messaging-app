/**
 * Unit tests: online-user table, duplicate prevention under concurrency,
 * and per-socket send serialization.
 */
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <thread>
#include <vector>

#include "network.h"
#include "session_manager.h"
#include "test_framework.h"

using namespace chat;

TEST_CASE(add_find_remove) {
    SessionManager m;
    auto a = std::make_shared<Session>("Rouchelle", 10);
    CHECK(m.add(a) == SessionManager::AddResult::Added);
    CHECK(m.is_online("Rouchelle"));
    CHECK(m.find("Rouchelle") == a);
    CHECK(m.find("Tanvi") == nullptr);
    m.remove(a);
    CHECK(!m.is_online("Rouchelle"));
    CHECK_EQ(m.count(), 0u);
}

TEST_CASE(duplicate_session_rejected) {
    SessionManager m;
    auto first = std::make_shared<Session>("Rouchelle", 10);
    auto second = std::make_shared<Session>("Rouchelle", 11);
    CHECK(m.add(first) == SessionManager::AddResult::Added);
    CHECK(m.add(second) == SessionManager::AddResult::AlreadyConnected);
    m.remove(second);  // must not remove the real session
    CHECK(m.find("Rouchelle") == first);
}

TEST_CASE(simultaneous_logins_same_name_only_one_wins) {
    for (int round = 0; round < 50; ++round) {
        SessionManager m;
        std::atomic<int> wins{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i) {
            threads.emplace_back([&, i] {
                if (m.add(std::make_shared<Session>("Rouchelle", 100 + i)) == SessionManager::AddResult::Added) ++wins;
            });
        }
        for (auto &t : threads) t.join();
        CHECK_EQ(wins.load(), 1);
    }
}

TEST_CASE(capacity_limit) {
    SessionManager m;
    CHECK(m.add(std::make_shared<Session>("a", 1), 2) == SessionManager::AddResult::Added);
    CHECK(m.add(std::make_shared<Session>("b", 2), 2) == SessionManager::AddResult::Added);
    CHECK(m.add(std::make_shared<Session>("c", 3), 2) == SessionManager::AddResult::ServerFull);
}

TEST_CASE(online_users_sorted) {
    SessionManager m;
    for (auto n : {"Prof.", "Rouchelle", "Jitendra"}) m.add(std::make_shared<Session>(n, 1));
    auto u = m.online_users();
    CHECK(u.size() == 3 && u[0] == "Jitendra" && u[1] == "Prof." && u[2] == "Rouchelle");
}

TEST_CASE(concurrent_senders_never_interleave_packets) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    auto s = std::make_shared<Session>("Tanvi", sv[0]);
    constexpr int kThreads = 6, kEach = 200;

    std::atomic<int> received{0}, corrupted{0};
    std::thread reader([&] {
        Packet p;
        while (received < kThreads * kEach && recv_packet(sv[1], p)) {
            // every payload is a run of one repeated letter; mixing = corruption
            std::string body = p.get_payload();
            if (body.size() != 900 || body.find_first_not_of(body[0]) != std::string::npos) ++corrupted;
            ++received;
        }
    });
    std::vector<std::thread> writers;
    for (int t = 0; t < kThreads; ++t) {
        writers.emplace_back([&, t] {
            Packet p = create_packet(ResponseType::MSG_REALTIME, "x", "Tanvi", std::string(900, char('a' + t)));
            for (int i = 0; i < kEach; ++i) s->send(p);
        });
    }
    for (auto &w : writers) w.join();
    reader.join();
    CHECK_EQ(received.load(), kThreads * kEach);
    CHECK_EQ(corrupted.load(), 0);
    ::close(sv[0]);
    ::close(sv[1]);
}

TEST_CASE(closed_session_does_not_write) {
    int sv[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    auto s = std::make_shared<Session>("Tanvi", sv[0]);
    s->mark_closed();
    CHECK(!s->send(create_packet(ResponseType::RESP_INFO)));
    ::close(sv[0]);
    ::close(sv[1]);
}

TEST_CASE(delivery_lock_is_stable_per_user) {
    SessionManager m;
    CHECK(&m.delivery_lock("Rouchelle") == &m.delivery_lock("Rouchelle"));
    CHECK(&m.delivery_lock("Rouchelle") != &m.delivery_lock("Tanvi"));
}

int main() { return test::run_all("session manager"); }
