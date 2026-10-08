// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "core/log.h"

#include <cstdio>
#include <deque>
#include <mutex>
#include <print>

namespace elg::detail {

namespace {

constexpr std::size_t kHistorySize = 200;

std::mutex g_mutex;
std::deque<std::string> g_history;

std::string_view level_name(LogLevel level) {
    switch (level) {
    case LogLevel::debug: return "debug";
    case LogLevel::info: return "info";
    case LogLevel::warning: return "warning";
    case LogLevel::error: return "error";
    }
    return "?";
}

} // namespace

void log_message(LogLevel level, std::string message) {
#ifdef NDEBUG
    if (level == LogLevel::debug) {
        return;
    }
#endif
    std::string line = std::format("[elg:{}] {}", level_name(level), message);

    std::lock_guard lock(g_mutex);
    std::println(stderr, "{}", line);
    g_history.push_back(std::move(line));
    if (g_history.size() > kHistorySize) {
        g_history.pop_front();
    }
}

std::vector<std::string> recent_log_lines() {
    std::lock_guard lock(g_mutex);
    return {g_history.begin(), g_history.end()};
}

} // namespace elg::detail
