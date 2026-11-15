// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "core/app_errors.h"

#include "core/log.h"

#include <cstdlib>
#include <memory>
#include <new>
#include <typeinfo>

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#endif

namespace elg::detail {

namespace {

bool g_reporting = false; // an App runs
bool g_handling = false;  // its error handler runs
bool g_stopped = false;   // its error handler ended the run: reports are only logged
struct Report {
    std::exception_ptr error;
    AppCode code;
};
std::vector<Report> g_reports;
std::exception_ptr g_fatal; // a std::bad_alloc: the run ends with it
std::exception_ptr g_quit;  // what the handler ended the run with

const char* describe(AppCode code) {
    switch (code) {
    case AppCode::body: return "a view's body";
    case AppCode::effect: return "an effect";
    case AppCode::event_handler: return "an event handler";
    case AppCode::task: return "a task";
    case AppCode::timer: return "a timer";
    case AppCode::lifecycle_action: return "an on_appear or on_disappear action";
    case AppCode::layout: return "layout";
    case AppCode::drawing: return "a Canvas's drawing";
    case AppCode::callback: return "a callback";
    }
    return "app code";
}

// A type's readable name: demangled where the compiler's ABI library can (GCC, Clang).
std::string name_of(const std::type_info& type) {
#if __has_include(<cxxabi.h>)
    int status = 0;
    const std::unique_ptr<char, void (*)(void*)> demangled(abi::__cxa_demangle(type.name(), nullptr, nullptr, &status),
                                                           std::free);
    if (status == 0 && demangled) {
        return demangled.get();
    }
#endif
    return type.name();
}

// The exception's type: Elegance's errors by name, others by their type_info.
std::string type_of(const std::exception_ptr& error) {
    try {
        std::rethrow_exception(error);
    } catch (const UsageError&) {
        return "elg::UsageError";
    } catch (const ResourceError&) {
        return "elg::ResourceError";
    } catch (const GpuError&) {
        return "elg::GpuError";
    } catch (const PlatformError&) {
        return "elg::PlatformError";
    } catch (const std::exception& other) {
        return name_of(typeid(other));
    } catch (...) {
        return "an unknown type";
    }
}

bool is_bad_alloc(const std::exception_ptr& error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::bad_alloc&) {
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

std::string exception_message(const std::exception_ptr& error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& known) {
        return known.what();
    } catch (...) {
        return "an exception of unknown type";
    }
}

void report_app_exception(const std::exception_ptr& error, AppCode code) {
    if (!g_reporting) {
        std::rethrow_exception(error);
    }
    log_error("{} threw {}: {}", describe(code), type_of(error), exception_message(error));
    if (is_bad_alloc(error)) {
        if (!g_fatal) {
            g_fatal = error; // out of memory: the run ends with it, whatever the handler says
        }
        return;
    }
    if (!g_handling && !g_stopped) {
        try {
            g_reports.push_back({error, code});
        } catch (...) {
            // Swallowed: out of memory, so the handler doesn't hear of it; it's in the log.
        }
    }
}

void begin_app_errors() {
    g_reporting = true;
    g_stopped = false;
    g_reports.clear();
    g_fatal = nullptr;
    g_quit = nullptr;
}

void end_app_errors() noexcept {
    g_reporting = false;
    g_reports.clear();
}

bool reporting_app_errors() noexcept {
    return g_reporting;
}

std::exception_ptr handle_app_errors(const AppErrorHandler& handler) {
    if (g_stopped || g_fatal) {
        return g_fatal ? g_fatal : g_quit;
    }
    for (Report& report : std::exchange(g_reports, {})) {
        if (!handler) {
            continue;
        }
        ErrorAction action = ErrorAction::resume;
        g_handling = true; // what it throws itself is only logged
        run_app_code(AppCode::callback, [&] { action = handler(report.error, report.code); });
        g_handling = false;
        if (action == ErrorAction::quit) {
            g_stopped = true;
            g_quit = report.error;
            g_reports.clear();
            break;
        }
    }
    return g_fatal ? g_fatal : g_quit;
}

} // namespace elg::detail
