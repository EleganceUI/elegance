// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// What the C tests of the C API need from the test build rather than from the C API itself
// (c_api_c_support.cpp): to be the UI thread of a running App, as App::run makes its thread, which
// the tests can't run without a display; a scope, as mounted views have; and another thread.

#ifndef ELG_TESTS_C_API_C_SUPPORT_H
#define ELG_TESTS_C_API_C_SUPPORT_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Makes the calling thread the UI thread of a running App, until elg_test_end_ui_thread().
void elg_test_begin_ui_thread(void);
// Releases (on this, the UI thread) what other threads handed over; true if anything.
bool elg_test_run_ui_releases(void);
void elg_test_end_ui_thread(void);

// Calls `work` with `context` inside a scope of its own, which then ends, as a view's does when
// it's removed.
void elg_test_in_scope(void (*work)(void* context), void* context);

// Calls `work` with `context` on another thread, and waits for it.
void elg_test_on_another_thread(void (*work)(void* context), void* context);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ELG_TESTS_C_API_C_SUPPORT_H
