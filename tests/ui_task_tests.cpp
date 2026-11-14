// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Tasks posted to the UI thread: their order, a task that throws, and the run they belong to.

#include "core/ui_tasks.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <vector>

using namespace elg;

TEST_CASE("a task that throws leaves the tasks after it to run, in order", "[tasks]") {
    std::vector<int> ran;
    detail::post_ui_task([&] { ran.push_back(1); });
    detail::post_ui_task([] { throw std::runtime_error("failed"); });
    detail::post_ui_task([&] { ran.push_back(3); });
    detail::post_ui_task([&] { ran.push_back(4); });
    CHECK_THROWS_AS(detail::run_ui_tasks(), std::runtime_error);
    CHECK(ran == std::vector<int>{1});

    detail::post_ui_task([&] { ran.push_back(5); }); // posted later: runs after them
    CHECK(detail::run_ui_tasks());
    CHECK(ran == std::vector<int>{1, 3, 4, 5});
}

TEST_CASE("Elegance's tasks for one run never run in the next; the app's wait for it", "[tasks]") {
    bool elegance_ran = false;
    bool app_ran = false;
    detail::begin_ui_tasks();
    detail::post_ui_task([&] { elegance_ran = true; });
    detail::post_app_task([&] { app_ran = true; });
    detail::end_ui_tasks(); // the run ends with both waiting
    detail::post_ui_task([&] { elegance_ran = true; }); // late, for the run that ended

    detail::begin_ui_tasks(); // the next run
    CHECK(detail::run_ui_tasks());
    CHECK(app_ran);
    CHECK_FALSE(elegance_ran);
    CHECK_FALSE(detail::run_ui_tasks());
    detail::end_ui_tasks();
}
