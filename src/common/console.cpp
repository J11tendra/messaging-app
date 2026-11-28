/**
 * @file console.cpp
 * @brief Mutex-protected stdout helpers.
 */
#include "console.h"

#include <ctime>
#include <iostream>
#include <mutex>

namespace chat {

namespace {
std::mutex g_console_mtx;
}

void log_event(const std::string &text) {
    std::time_t now = std::time(nullptr);
    std::tm tm_now{};
    localtime_r(&now, &tm_now);
    char stamp[16];
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &tm_now);

    std::lock_guard<std::mutex> lock(g_console_mtx);
    std::cout << "[" << stamp << "] " << text << std::endl;
}

void console_print(const std::string &text, bool reprompt) {
    std::lock_guard<std::mutex> lock(g_console_mtx);
    // "\r" moves back over a "> " prompt that may already be on the line.
    std::cout << "\r" << text << "\n";
    if (reprompt) std::cout << "> ";
    std::cout << std::flush;
}

void console_prompt() {
    std::lock_guard<std::mutex> lock(g_console_mtx);
    std::cout << "> " << std::flush;
}

}  // namespace chat
