// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Objects that must be destroyed on the UI thread (media sources, audio players): what other
// threads let go of comes back to it.

#include "core/ui_tasks.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <thread>
#include <utility>

using namespace elg;

namespace {

// Records the thread that destroyed it.
class Tracked {
public:
    explicit Tracked(std::thread::id& destroyed_on) : destroyed_on_(&destroyed_on) {}
    ~Tracked() { *destroyed_on_ = std::this_thread::get_id(); }
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    Tracked(Tracked&&) = delete;
    Tracked& operator=(Tracked&&) = delete;

private:
    std::thread::id* destroyed_on_;
};

// Drops `object` on a thread of its own.
void let_go_elsewhere(std::shared_ptr<Tracked> object) {
    std::thread([dropped = std::move(object)]() mutable { dropped.reset(); }).join();
}

} // namespace

TEST_CASE("what another thread lets go of is destroyed on the UI thread", "[threads]") {
    detail::begin_ui_releases();
    std::thread::id destroyed_on;
    let_go_elsewhere(detail::make_ui_shared<Tracked>(destroyed_on));
    CHECK(destroyed_on == std::thread::id()); // waiting for the UI thread
    CHECK(detail::run_ui_releases());
    CHECK(destroyed_on == std::this_thread::get_id());
    CHECK_FALSE(detail::run_ui_releases()); // nothing left
    detail::end_ui_releases();
}

TEST_CASE("what the UI thread lets go of is destroyed at once", "[threads]") {
    detail::begin_ui_releases();
    std::thread::id destroyed_on;
    auto object = detail::make_ui_shared<Tracked>(destroyed_on);
    object.reset();
    CHECK(destroyed_on == std::this_thread::get_id());
    detail::end_ui_releases();
}

TEST_CASE("when the run ends, what's still waiting is destroyed on the UI thread", "[threads]") {
    detail::begin_ui_releases();
    std::thread::id destroyed_on;
    let_go_elsewhere(detail::make_ui_shared<Tracked>(destroyed_on));
    detail::end_ui_releases();
    CHECK(destroyed_on == std::this_thread::get_id());

    // After it, nothing waits for a UI thread: the thread that lets go destroys.
    std::thread::id after_on;
    let_go_elsewhere(detail::make_ui_shared<Tracked>(after_on));
    CHECK(after_on != std::thread::id());
    CHECK(after_on != std::this_thread::get_id());
}
