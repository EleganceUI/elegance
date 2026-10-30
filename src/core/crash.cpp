// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "core/crash.h"

#include "core/log.h"
#include "platform/generic/platform.h"

#include <elg/app.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <print>
#include <sstream>
#include <thread>

#if !defined(__cpp_lib_stacktrace)
#include <execinfo.h>
#endif

namespace elg::detail {

#if defined(__cpp_lib_stacktrace)
std::string to_text(const StackTrace& trace) {
    return std::to_string(trace);
}
#else
StackTrace StackTrace::current() noexcept {
    StackTrace trace;
    try {
        trace.frames.resize(128);
        const int count = backtrace(trace.frames.data(), static_cast<int>(trace.frames.size()));
        trace.frames.resize(static_cast<std::size_t>(std::max(count, 0)));
    } catch (...) {
        trace.frames.clear();
    }
    return trace;
}

std::string to_text(const StackTrace& trace) {
    std::string text;
    if (trace.frames.empty()) {
        return "(unavailable)";
    }
    char** symbols = backtrace_symbols(trace.frames.data(), static_cast<int>(trace.frames.size()));
    for (std::size_t i = 0; i < trace.frames.size(); ++i) {
        text += symbols ? symbols[i] : std::format("{}", trace.frames[i]);
        text += '\n';
    }
    std::free(symbols);
    return text;
}
#endif

namespace {

std::atomic_flag g_crashing;

std::string build_report(std::string_view message, const std::source_location& where,
                         const StackTrace& trace) {
    std::ostringstream thread_id;
    thread_id << std::this_thread::get_id();

    std::string report = std::format("Elegance internal error\n"
                                     "version:  {}\n"
                                     "thread:   {}\n"
                                     "location: {}:{} ({})\n"
                                     "message:  {}\n\n"
                                     "stack trace:\n{}\n\n"
                                     "recent log:\n",
                                     version(), thread_id.str(), where.file_name(), where.line(),
                                     where.function_name(), message, to_text(trace));
    for (const std::string& line : recent_log_lines()) {
        report += line;
        report += '\n';
    }
    return report;
}

std::filesystem::path write_report(const std::string& report) {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    const std::filesystem::path dir = platform::pref_path() / "crashes";
    std::filesystem::create_directories(dir);
    const std::filesystem::path path = dir / std::format("crash-{:%Y%m%d-%H%M%S}.txt", now);
    std::ofstream file(path);
    file << report;
    return path;
}

[[noreturn]] void on_terminate() noexcept {
    std::string message = "std::terminate called";
    if (std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& e) {
            message += std::format(" with an uncaught exception: {}", e.what());
        } catch (...) {
            message += " with an uncaught exception of unknown type";
        }
    }
    internal_error(message);
}

} // namespace

void internal_error(std::string_view message, std::source_location where, StackTrace trace) {
    // A crash while reporting a crash: give up immediately.
    if (g_crashing.test_and_set()) {
        std::abort();
    }

    std::string report;
    std::string path;
    try {
        report = build_report(message, where, trace);
        std::println(stderr, "{}", report);
        path = write_report(report).string();
    } catch (...) {
        if (report.empty()) {
            std::fputs("Elegance internal error (report formatting failed)\n", stderr);
        }
    }

    if (platform::debugger_present()) {
        platform::debug_break();
    }

    const std::string box =
        path.empty() ? std::string("An internal error occurred in Elegance.")
                     : "An internal error occurred in Elegance.\nA crash report was written to:\n" +
                           path;
    platform::show_error_box("Elegance crashed", box.c_str());
    std::abort();
}

CrashHandlerScope::CrashHandlerScope() : previous_(std::set_terminate(&on_terminate)) {}

CrashHandlerScope::~CrashHandlerScope() {
    std::set_terminate(previous_);
}

} // namespace elg::detail
