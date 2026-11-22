// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// What every run of the tests needs before Catch2 starts, and the tests' main().

#include "no_error_dialogs.h"

#include "core/log.h"

#include <SDL3/SDL_stdinc.h>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <atomic>
#include <cstdio>
#include <mutex>
#include <print>
#include <string>
#include <vector>

namespace {

// Nobody is there to close a dialog: failed checks and crashes fail the test, on stderr.
const bool g_no_dialogs = [] {
    elegance_apps::no_error_dialogs();
    return true;
}();

// The tests don't read or write the pipeline cache of whoever runs them: an app that a test starts would keep its
// shaders in their folder, and runs would change each other. The one test of the cache gives it a folder of its
// own (ELGSPEC-0036.R4). A setting that the person running the tests made is left alone.
const bool g_no_pipeline_cache = [] {
    // Through SDL, which reads its own copy of the environment: the cache's folder is chosen from that one. A
    // setting that is there is left (the last argument).
    SDL_setenv_unsafe("ELG_PIPELINE_CACHE", "off", 0);
    return true;
}();

// Bugs Elegance carried on after (WebGPU validation errors) during the current test, from any
// thread: they fail the test.
std::mutex g_mutex;
std::vector<std::string> g_internal_errors;
std::atomic<bool> g_failed{false};

void observe(std::string_view message) noexcept {
    try {
        const std::lock_guard lock(g_mutex);
        g_internal_errors.emplace_back(message);
    } catch (...) {
        g_failed = true; // not even recorded: the run still fails
    }
}

const bool g_observing = [] {
    elg::detail::set_internal_error_observer(&observe);
    return true;
}();

// Catch2 counts a test's assertions before listeners hear it ended, so a test with such an error
// fails through the run's exit code; ctest runs each test on its own, so that test fails.
class InternalErrors final : public Catch::EventListenerBase {
public:
    using EventListenerBase::EventListenerBase;

    void testCaseStarting(const Catch::TestCaseInfo&) override {
        const std::lock_guard lock(g_mutex);
        g_internal_errors.clear();
    }

    void testCaseEnded(const Catch::TestCaseStats& stats) override {
        std::vector<std::string> errors;
        {
            const std::lock_guard lock(g_mutex);
            errors.swap(g_internal_errors);
        }
        if (errors.empty()) {
            return;
        }
        g_failed = true;
        std::println(stderr, "FAILED: \"{}\" had {} internal error(s) of Elegance:", stats.testInfo->name,
                     errors.size());
        for (const std::string& error : errors) {
            std::println(stderr, "    {}", error);
        }
    }
};

} // namespace

CATCH_REGISTER_LISTENER(InternalErrors)

int main(int argc, char* argv[]) {
    const int result = Catch::Session().run(argc, argv);
    return result != 0 ? result : (g_failed ? 1 : 0);
}
