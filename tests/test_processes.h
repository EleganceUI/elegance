// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// For tests of child processes on Linux and macOS.

#ifndef ELG_TESTS_TEST_PROCESSES_H
#define ELG_TESTS_TEST_PROCESSES_H

#if !defined(_WIN32)

#include <signal.h>

#include <cerrno>
#include <chrono>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace elg_tests {

// Whether process `pid` has ended within `timeout`: it's gone, or it's a zombie that the process
// it was handed to hasn't reaped yet (Linux; the processes a killed shell started are).
inline bool process_ends(int pid, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (kill(pid, 0) != 0 && errno == ESRCH) {
            return true;
        }
#if defined(__linux__)
        std::string stat;
        try {
            std::ifstream file(std::format("/proc/{}/stat", pid));
            stat.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        } catch (const std::exception&) {
            // Gone while its stat was read, which libstdc++ reports by throwing (ESRCH): kill()
            // says so on the next turn.
            continue;
        }
        if (const auto name_end = stat.rfind(')'); name_end != std::string::npos && name_end + 2 < stat.size()) {
            const char state = stat[name_end + 2];
            if (state == 'Z' || state == 'X') {
                return true;
            }
        }
#endif
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

} // namespace elg_tests

#endif

#endif // ELG_TESTS_TEST_PROCESSES_H
