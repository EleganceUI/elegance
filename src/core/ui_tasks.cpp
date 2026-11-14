// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "core/ui_tasks.h"

#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace elg::detail {

namespace {

std::mutex g_mutex;
std::vector<std::function<void()>> g_tasks;
std::function<void()> g_wake;

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
    std::function<void()> wake;
    {
        const std::lock_guard lock(g_mutex);
        g_tasks.push_back(std::move(task));
        wake = g_wake;
    }
    if (wake) {
        wake();
    }
}

void set_ui_task_waker(std::function<void()> wake) {
    const std::lock_guard lock(g_mutex);
    g_wake = std::move(wake);
}

bool run_ui_tasks() {
    std::vector<std::function<void()>> tasks;
    {
        const std::lock_guard lock(g_mutex);
        tasks.swap(g_tasks);
    }
    for (auto& task : tasks) {
        task();
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
