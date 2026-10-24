// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CORE_CRASH_H
#define ELG_CORE_CRASH_H

#include <exception>
#include <format>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>
#include <version>

#if defined(__cpp_lib_stacktrace)
#include <stacktrace>
#endif

namespace elg::detail {

#if defined(__cpp_lib_stacktrace)
using StackTrace = std::stacktrace;
#else
// libc++ (macOS) has no <stacktrace>: the return addresses, symbolized by execinfo.
struct StackTrace {
    static StackTrace current() noexcept;
    std::vector<void*> frames;
};
#endif
std::string to_text(const StackTrace& trace);

// Reports a bug inside Elegance: writes a crash report, shows it, then terminates
// (design.md §3.2). Never use this for errors the developer can handle; throw elg::Error.
[[noreturn]] void internal_error(std::string_view message,
                                 std::source_location where = std::source_location::current(),
                                 StackTrace trace = StackTrace::current());

// Routes std::terminate to internal_error() while alive, then restores the previous handler.
class CrashHandlerScope {
public:
    CrashHandlerScope();
    ~CrashHandlerScope();

    CrashHandlerScope(const CrashHandlerScope&) = delete;
    CrashHandlerScope& operator=(const CrashHandlerScope&) = delete;

private:
    std::terminate_handler previous_;
};

} // namespace elg::detail

// Always-on check for internal invariants. Usage: ELG_ASSERT(index < size, "index {}", index);
#define ELG_ASSERT(cond, ...)                                                                      \
    do {                                                                                           \
        if (!(cond)) [[unlikely]] {                                                                \
            ::elg::detail::internal_error(                                                         \
                std::format("assertion failed: {}: {}", #cond, std::format(__VA_ARGS__)));         \
        }                                                                                          \
    } while (false)

#ifdef NDEBUG
#define ELG_DEBUG_ASSERT(cond, ...) ((void)0)
#else
#define ELG_DEBUG_ASSERT(cond, ...) ELG_ASSERT(cond, __VA_ARGS__)
#endif

#endif // ELG_CORE_CRASH_H
