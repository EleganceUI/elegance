// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The test build's side of c_api_c_support.h.

#include "c_api_c_support.h"

#include "core/ui_tasks.h"
#include "reactive/runtime.h"

#include <elg/reactive.h>

#include <optional>
#include <thread>

void elg_test_begin_ui_thread(void) {
    elg::detail::set_ui_thread(std::this_thread::get_id()); // as App::run does
    elg::detail::begin_ui_releases();
}

bool elg_test_run_ui_releases(void) {
    return elg::detail::run_ui_releases();
}

void elg_test_end_ui_thread(void) {
    elg::detail::end_ui_releases();
    elg::detail::set_ui_thread(std::nullopt);
}

void elg_test_in_scope(void (*work)(void* context), void* context) {
    elg::detail::Scope scope;
    const elg::detail::Scope::Use use(scope);
    work(context);
}

void elg_test_on_another_thread(void (*work)(void* context), void* context) {
    std::thread(work, context).join();
}
