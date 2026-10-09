// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "core/ui_tasks.h"

#include <mutex>
#include <utility>
#include <vector>

namespace elg::detail {

namespace {

std::mutex g_mutex;
std::vector<std::function<void()>> g_tasks;
std::function<void()> g_wake;

} // namespace

void post_ui_task(std::function<void()> task) {
    std::function<void()> wake;
    {
        std::lock_guard lock(g_mutex);
        g_tasks.push_back(std::move(task));
        wake = g_wake;
    }
    if (wake) {
        wake();
    }
}

void set_ui_task_waker(std::function<void()> wake) {
    std::lock_guard lock(g_mutex);
    g_wake = std::move(wake);
}

bool run_ui_tasks() {
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard lock(g_mutex);
        tasks.swap(g_tasks);
    }
    for (auto& task : tasks) {
        task();
    }
    return !tasks.empty();
}

} // namespace elg::detail
