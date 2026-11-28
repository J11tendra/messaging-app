/**
 * @file console.h
 * @brief Thread-safe console output.
 *
 * Both processes write to stdout from several threads (server: one worker per
 * client; client: CLI thread + receiver thread). A single mutex keeps lines
 * from interleaving.
 */
#ifndef CONSOLE_H
#define CONSOLE_H

#include <string>

namespace chat {

/** Server event log: prints "[HH:MM:SS] <text>". */
void log_event(const std::string &text);

/** Client output: prints text on its own line, then re-prints the "> " prompt. */
void console_print(const std::string &text, bool reprompt = true);

/** Prints the "> " prompt. */
void console_prompt();

}  // namespace chat

#endif  // CONSOLE_H
