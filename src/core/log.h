// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CORE_LOG_H
#define ELG_CORE_LOG_H

#include <format>
#include <string>
#include <vector>

namespace elg::detail {

enum class LogLevel { debug, info, warning, error };

// Thread-safe. Writes to stderr and keeps recent lines for crash reports.
void log_message(LogLevel level, std::string message);

// The most recent log lines, oldest first.
std::vector<std::string> recent_log_lines();

template <class... Args>
void log_debug(std::format_string<Args...> fmt, Args&&... args) {
    log_message(LogLevel::debug, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void log_info(std::format_string<Args...> fmt, Args&&... args) {
    log_message(LogLevel::info, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void log_warning(std::format_string<Args...> fmt, Args&&... args) {
    log_message(LogLevel::warning, std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void log_error(std::format_string<Args...> fmt, Args&&... args) {
    log_message(LogLevel::error, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace elg::detail

#endif // ELG_CORE_LOG_H
