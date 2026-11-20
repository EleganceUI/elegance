// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "core/ui_tasks.h"

#include "core/app_errors.h"
#include "core/profile.h"

#include <cstdint>
#include <iterator>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace elg::detail {

namespace {

// A posted task: the app's (post_to_ui_thread) waits for an App to run it; Elegance's belongs to
// the App::run that was running, or last ran, when it was posted.
struct Task {
    std::function<void()> work;
    bool app = false;
    std::uint64_t run = 0;
};

std::mutex g_mutex;
// Leaked: tasks the app posted that no App ran are never destroyed during static destruction,
// when what their closures need (a binding's runtime) may be gone.
std::vector<Task>& g_tasks = *new std::vector<Task>();
std::function<void()> g_wake;
std::uint64_t g_run = 0; // the App::run going on, or the last one

// Takes Elegance's tasks that belong to another run than `run`, to destroy outside the lock.
std::vector<Task> take_stale(std::uint64_t run) {
    const std::lock_guard lock(g_mutex);
    std::vector<Task> stale;
    std::erase_if(g_tasks, [&](Task& task) {
        if (task.app || task.run == run) {
            return false;
        }
        stale.push_back(std::move(task));
        return true;
    });
    return stale;
}

void post(Task task) {
    std::function<void()> wake;
    {
        const std::lock_guard lock(g_mutex);
        task.run = g_run;
        g_tasks.push_back(std::move(task));
        wake = g_wake;
    }
    if (wake) {
        wake();
    }
}

struct Release {
    void* object;
    void (*destroy)(void*) noexcept;
};
// The running App's UI thread (none while no App runs), and what other threads handed it.
std::thread::id g_release_thread;
// Leaked: objects handed over that no thread destroyed before the process ends stay reachable,
// rather than being destroyed during static destruction, when the threads they use are gone.
std::vector<Release>& g_releases = *new std::vector<Release>();
// The process's main thread, recorded while the program loads (static initialization runs on
// it). Outside a run it takes the UI thread's place: other threads hand it what they release.
const std::thread::id g_main_thread = std::this_thread::get_id();

std::vector<Release> take_releases() {
    const std::lock_guard lock(g_mutex);
    return std::exchange(g_releases, {});
}

} // namespace

void post_ui_task(std::function<void()> task) {
    post(Task{.work = std::move(task)});
}

void post_app_task(std::function<void()> task) {
    post(Task{.work = std::move(task), .app = true});
}

void set_ui_task_waker(std::function<void()> wake) {
    const std::lock_guard lock(g_mutex);
    g_wake = std::move(wake);
}

void begin_ui_tasks() {
    std::uint64_t run = 0;
    {
        const std::lock_guard lock(g_mutex);
        run = ++g_run;
    }
    take_stale(run); // Elegance's tasks from earlier runs: destroyed, not run
}

void end_ui_tasks() {
    take_stale(0); // all of Elegance's: they refer to this run
}

bool run_ui_tasks() {
    std::vector<Task> tasks;
    {
        const std::lock_guard lock(g_mutex);
        tasks.swap(g_tasks);
    }
    if (tasks.empty()) {
        return false;
    }
    ELG_ZONE("UI tasks");
    for (auto task = tasks.begin(); task != tasks.end(); ++task) {
        try {
            task->work();
        } catch (...) {
            if (reporting_app_errors()) { // while an App runs: reported, and the others run on
                report_app_exception(std::current_exception(), AppCode::task);
                continue;
            }
            // The tasks after it run later, still first and in order.
            const std::lock_guard lock(g_mutex);
            g_tasks.insert(g_tasks.begin(), std::make_move_iterator(task + 1), std::make_move_iterator(tasks.end()));
            throw;
        }
    }
    return true;
}

void deliver_foreign_exception(const std::exception_ptr& error) noexcept {
    try {
        post_ui_task([error] { report_app_exception(error, AppCode::callback); });
    } catch (...) {
        // Swallowed: out of memory, so this one is lost; nothing may leave a foreign callback.
    }
}

void release_on_ui_thread(void* object, void (*destroy)(void*) noexcept) noexcept {
    bool handed_over = false;
    bool collect = false;
    std::function<void()> wake;
    {
        const std::lock_guard lock(g_mutex);
        // While an App runs, its UI thread destroys these; outside a run, the main thread does.
        const bool running = g_release_thread != std::thread::id();
        const std::thread::id ui = running ? g_release_thread : g_main_thread;
        if (ui != std::this_thread::get_id()) {
            try {
                g_releases.push_back({object, destroy});
                handed_over = true;
                wake = g_wake;
            } catch (...) {
                // Swallowed: out of memory, so it's destroyed here, rather than not at all.
            }
        } else {
            // Outside a run, what other threads handed over waits for the main thread: now.
            collect = !running && !g_releases.empty();
        }
    }
    if (!handed_over) {
        destroy(object);
    } else if (wake) {
        wake();
    }
    if (collect) {
        run_ui_releases();
    }
}

void begin_ui_releases() {
    const std::lock_guard lock(g_mutex);
    g_release_thread = std::this_thread::get_id();
}

bool run_ui_releases() {
    const std::vector<Release> releases = take_releases();
    for (const Release& release : releases) {
        release.destroy(release.object);
    }
    return !releases.empty();
}

void end_ui_releases() {
    while (true) {
        std::vector<Release> releases;
        {
            const std::lock_guard lock(g_mutex);
            releases = std::exchange(g_releases, {});
            if (releases.empty()) {
                g_release_thread = std::thread::id(); // under the lock: none is handed over too late
                return;
            }
        }
        for (const Release& release : releases) {
            release.destroy(release.object); // which may release more
        }
    }
}

} // namespace elg::detail
