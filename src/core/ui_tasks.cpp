// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "core/ui_tasks.h"

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
std::vector<Release> g_releases;

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
    for (auto task = tasks.begin(); task != tasks.end(); ++task) {
        try {
            task->work();
        } catch (...) {
            // The tasks after it run later, still first and in order.
            const std::lock_guard lock(g_mutex);
            g_tasks.insert(g_tasks.begin(), std::make_move_iterator(task + 1), std::make_move_iterator(tasks.end()));
            throw;
        }
    }
    return !tasks.empty();
}

void release_on_ui_thread(void* object, void (*destroy)(void*) noexcept) noexcept {
    bool handed_over = false;
    std::function<void()> wake;
    {
        const std::lock_guard lock(g_mutex);
        if (g_release_thread != std::thread::id() && g_release_thread != std::this_thread::get_id()) {
            try {
                g_releases.push_back({object, destroy});
                handed_over = true;
                wake = g_wake;
            } catch (...) { // out of memory: here, then, rather than not at all
            }
        }
    }
    if (!handed_over) {
        destroy(object);
    } else if (wake) {
        wake();
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
