/**
 * Unit tests: valid-user list loading and validation.
 */
#include <unistd.h>

#include <cstdio>
#include <fstream>

#include "test_framework.h"
#include "user_registry.h"

using namespace chat;

static std::string write_file(const std::string &content) {
    std::string path = "/tmp/msgapp_users_" + std::to_string(::getpid()) + ".txt";
    std::ofstream(path) << content;
    return path;
}

TEST_CASE(loads_names_ignoring_comments_and_blanks) {
    UserRegistry r;
    std::string err;
    auto path = write_file("# comment\nRouchelle\n\n  Tanvi  \r\nProf.\n");
    CHECK(r.load_from_file(path, err));
    CHECK_EQ(r.size(), 3u);
    CHECK(r.is_valid("Rouchelle"));
    CHECK(r.is_valid("Tanvi"));  // trimmed
    CHECK(!r.is_valid("mallory"));
    CHECK(!r.is_valid("# comment"));
    std::remove(path.c_str());
}

TEST_CASE(rejects_malformed_or_empty_files) {
    UserRegistry r;
    std::string err;
    auto path = write_file("Rouchelle\nbad name\n");
    CHECK(!r.load_from_file(path, err));
    CHECK(!err.empty());
    path = write_file("# nobody\n");
    CHECK(!r.load_from_file(path, err));
    CHECK(!r.load_from_file("/nonexistent/users.txt", err));
    std::remove(path.c_str());
}

TEST_CASE(username_rules) {
    CHECK(UserRegistry::is_well_formed("user_1.a-b"));
    CHECK(!UserRegistry::is_well_formed(""));
    CHECK(!UserRegistry::is_well_formed("server"));
    CHECK(!UserRegistry::is_well_formed("has space"));
    CHECK(!UserRegistry::is_well_formed(std::string(32, 'a')));
    CHECK(UserRegistry::is_well_formed(std::string(31, 'a')));
}

TEST_CASE(lookup_is_case_sensitive) {
    UserRegistry r;
    r.add("Rouchelle");
    CHECK(!r.is_valid("rouchelle"));
}

int main() { return test::run_all("user registry"); }
