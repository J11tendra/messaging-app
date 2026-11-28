/**
 * @file test_framework.h
 * @brief Minimal unit-test helpers (no external dependency).
 *        CHECK keeps running after a failure; the program exits non-zero if any failed.
 */
#ifndef TEST_FRAMEWORK_H
#define TEST_FRAMEWORK_H

#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace test {

inline int &failures() { static int f = 0; return f; }

struct Case { std::string name; std::function<void()> fn; };
inline std::vector<Case> &cases() { static std::vector<Case> c; return c; }

struct Register {
    Register(const std::string &n, std::function<void()> f) { cases().push_back({n, std::move(f)}); }
};

inline int run_all(const std::string &suite) {
    std::cout << "=== " << suite << " ===\n";
    for (auto &c : cases()) {
        const int before = failures();
        c.fn();
        std::cout << (failures() == before ? "  [PASS] " : "  [FAIL] ") << c.name << "\n";
    }
    std::cout << (failures() ? "FAILED: " : "OK: ") << cases().size() << " test(s), "
              << failures() << " failed check(s)\n\n";
    return failures() ? 1 : 0;
}

}  // namespace test

#define TEST_CASE(name)                                                   \
    static void name();                                                   \
    static test::Register reg_##name(#name, name);                        \
    static void name()

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            ++test::failures();                                                       \
            std::cout << "    check failed: " #cond " (" __FILE__ ":" << __LINE__ << ")\n"; \
        }                                                                             \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

#endif  // TEST_FRAMEWORK_H
