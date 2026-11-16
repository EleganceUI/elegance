// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Which thread may run an App.

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <thread>

using namespace elg;

namespace {

// What App::run() threw, if anything: its message.
std::string run_error(App& app) {
    try {
        app.run();
    } catch (const UsageError& error) {
        return error.what();
    }
    return {};
}

} // namespace

TEST_CASE("App::run() refuses a thread other than the main one", "[app]") {
    App app;
    std::string off_main;
    std::thread([&] { off_main = run_error(app); }).join();
    CHECK(off_main.find("main thread") != std::string::npos);
    // On the main thread it goes on to its other checks: here, that there's no window.
    CHECK(run_error(app).find("no windows") != std::string::npos);
}
