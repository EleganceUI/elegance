// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_CORE_UI_TASKS_H
#define ELG_CORE_UI_TASKS_H

#include <functional>
#include <memory>
#include <utility>

namespace elg::detail {

// Work handed to the UI thread from other threads (e.g. a finished hot-reload build). The App
// runs posted tasks between events, where state may be written; posting wakes its event loop.
// Thread-safe.
void post_ui_task(std::function<void()> task);

// App side: what wakes the event loop (nullptr while no App runs), and running what's queued.
// Returns true if anything ran.
void set_ui_task_waker(std::function<void()> wake);
bool run_ui_tasks();

// Objects that must be destroyed on the UI thread: those that own reactive state or a platform
// media, capture or audio engine. While an App runs, a thread that drops the last reference hands
// the object to the UI thread, which destroys it between events (waking its loop); otherwise
// it's destroyed at once. Thread-safe.
void release_on_ui_thread(void* object, void (*destroy)(void*) noexcept) noexcept;

template <class T>
struct ReleaseOnUiThread {
    void operator()(T* object) const noexcept {
        release_on_ui_thread(object, [](void* released) noexcept { delete static_cast<T*>(released); });
    }
};

// make_shared for such objects: wherever the last reference goes, the object is destroyed on the
// UI thread.
template <class T, class... Args>
std::shared_ptr<T> make_ui_shared(Args&&... args) {
    return std::shared_ptr<T>(new T(std::forward<Args>(args)...), ReleaseOnUiThread<T>{});
}

// App side: from begin_ui_releases() on, other threads hand objects to this thread; run_ui_releases()
// destroys what they handed over (true if anything), and end_ui_releases() what's left, then stops
// taking more.
void begin_ui_releases();
bool run_ui_releases();
void end_ui_releases();

} // namespace elg::detail

#endif // ELG_CORE_UI_TASKS_H
