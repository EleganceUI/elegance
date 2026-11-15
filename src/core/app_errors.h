// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CORE_APP_ERRORS_H
#define ELG_CORE_APP_ERRORS_H

// Exceptions that escape app code. While an App runs, each one is written to the log and kept
// for the app's error handler, which the App calls between events; the app goes on. A
// std::bad_alloc ends the run instead. Outside a run (tests, views built before App::run) the
// exception goes on to the caller. UI thread.

#include <elg/error.h>

#include <exception>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace elg::detail {

// Reports `error`, which escaped app code of kind `code`; outside a run, rethrows it.
void report_app_exception(const std::exception_ptr& error, AppCode code);

// Runs `function`, app code of kind `code`. What escapes it is reported (or, outside a run,
// goes on). Returns whether it returned normally.
template <class F>
bool run_app_code(AppCode code, F&& function) {
    try {
        std::forward<F>(function)();
        return true;
    } catch (...) {
        report_app_exception(std::current_exception(), code);
        return false;
    }
}

// An exception's what(), or that its type is unknown.
std::string exception_message(const std::exception_ptr& error);

// App side. begin_app_errors() starts reporting for a run, end_app_errors() stops.
void begin_app_errors();
void end_app_errors() noexcept;
bool reporting_app_errors() noexcept;
// Passes what was reported since the last call to `handler`, oldest first (with no handler, it's
// dropped: it's in the log). What the handler throws is only logged. Returns the exception the
// run ends with: the one the handler answered ErrorAction::quit to, from then on for good, or a
// std::bad_alloc that escaped app code; else null.
using AppErrorHandler = std::function<ErrorAction(const std::exception_ptr& error, AppCode code)>;
std::exception_ptr handle_app_errors(const AppErrorHandler& handler);

} // namespace elg::detail

#endif // ELG_CORE_APP_ERRORS_H
