// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CORE_UI_TASKS_H
#define ELG_CORE_UI_TASKS_H

#include <functional>

namespace elg::detail {

// Work handed to the UI thread from other threads (e.g. a finished hot-reload build). The App
// runs posted tasks between events, where state may be written; posting wakes its event loop.
// Thread-safe.
void post_ui_task(std::function<void()> task);

// App side: what wakes the event loop (nullptr while no App runs), and running what's queued.
// Returns true if anything ran.
void set_ui_task_waker(std::function<void()> wake);
bool run_ui_tasks();

} // namespace elg::detail

#endif // ELG_CORE_UI_TASKS_H
