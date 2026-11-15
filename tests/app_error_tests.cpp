// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Exceptions that escape app code while an App runs: reported, and the app goes on; the error
// handler hears them and may end the run. A run is stood in for with begin_app_errors().

#include "core/app_errors.h"
#include "core/timers.h"
#include "core/ui_tasks.h"
#include "reactive/runtime.h"
#include "render/display_list.h"
#include "text/text_system.h"
#include "view/view_node.h"

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace elg;

namespace {

// Reporting as while an App runs, until it goes.
struct Run {
    Run() { detail::begin_app_errors(); }
    ~Run() { detail::end_app_errors(); }
    Run(const Run&) = delete;
    Run& operator=(const Run&) = delete;
    Run(Run&&) = delete;
    Run& operator=(Run&&) = delete;
};

// What the handler heard.
struct Heard {
    std::vector<std::string> messages;
    std::vector<AppCode> codes;
};

detail::AppErrorHandler recorder(Heard& heard, ErrorAction answer = ErrorAction::resume) {
    return [&heard, answer](const std::exception_ptr& error, AppCode code) {
        heard.messages.push_back(detail::exception_message(error));
        heard.codes.push_back(code);
        return answer;
    };
}

} // namespace

TEST_CASE("while an App runs, a task that throws is reported and the next tasks run", "[app-errors]") {
    const Run run;
    std::vector<std::string> ran;
    detail::post_ui_task([] { throw std::runtime_error("first"); });
    detail::post_ui_task([&] { ran.emplace_back("second"); });
    CHECK_NOTHROW(detail::run_ui_tasks());
    CHECK(ran == std::vector<std::string>{"second"});
    Heard heard;
    CHECK_FALSE(detail::handle_app_errors(recorder(heard)));
    CHECK(heard.messages == std::vector<std::string>{"first"});
    CHECK(heard.codes == std::vector<AppCode>{AppCode::task});
}

TEST_CASE("while an App runs, an effect that throws is reported and the flush goes on", "[app-errors]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<int> trigger(0);
    std::vector<int> seen;
    effect([=] {
        if (trigger() > 0) {
            throw std::runtime_error("effect");
        }
    });
    effect([=, &seen] { seen.push_back(trigger()); });
    const Run run;
    trigger = 1;
    CHECK_NOTHROW(detail::flush_effects());
    CHECK(seen == std::vector<int>{0, 1});
    Heard heard;
    detail::handle_app_errors(recorder(heard));
    CHECK(heard.codes == std::vector<AppCode>{AppCode::effect});
}

TEST_CASE("while an App runs, a lifecycle action that throws leaves the others to run", "[app-errors]") {
    const Run run;
    std::vector<std::string> ran;
    detail::queue_lifecycle_action([] { throw std::runtime_error("appear"); });
    detail::queue_lifecycle_action([&] { ran.emplace_back("next"); });
    CHECK_NOTHROW(detail::run_lifecycle_actions());
    CHECK(ran == std::vector<std::string>{"next"});
    Heard heard;
    detail::handle_app_errors(recorder(heard));
    CHECK(heard.codes == std::vector<AppCode>{AppCode::lifecycle_action});
}

TEST_CASE("the error handler can end the run; then it hears nothing more", "[app-errors]") {
    const Run run;
    detail::post_ui_task([] { throw std::runtime_error("one"); });
    detail::post_ui_task([] { throw std::runtime_error("two"); });
    detail::run_ui_tasks();
    Heard heard;
    const std::exception_ptr end = detail::handle_app_errors(recorder(heard, ErrorAction::quit));
    REQUIRE(end);
    CHECK(detail::exception_message(end) == "one");
    detail::post_ui_task([] { throw std::runtime_error("three"); });
    detail::run_ui_tasks();
    CHECK(detail::handle_app_errors(recorder(heard)) == end);
    CHECK(heard.messages == std::vector<std::string>{"one"});
}

TEST_CASE("what the error handler throws is only logged", "[app-errors]") {
    const Run run;
    detail::post_ui_task([] { throw std::runtime_error("task"); });
    detail::run_ui_tasks();
    int calls = 0;
    CHECK_FALSE(detail::handle_app_errors([&](const std::exception_ptr&, AppCode) -> ErrorAction {
        ++calls;
        throw std::runtime_error("handler");
    }));
    CHECK(calls == 1);
    CHECK_FALSE(detail::handle_app_errors([&](const std::exception_ptr&, AppCode) {
        ++calls;
        return ErrorAction::resume;
    }));
    CHECK(calls == 1); // the handler's own exception never reached it
}

TEST_CASE("running out of memory in app code ends the run, without the handler", "[app-errors]") {
    const Run run;
    detail::post_ui_task([] { throw std::bad_alloc(); });
    detail::run_ui_tasks();
    Heard heard;
    CHECK(detail::handle_app_errors(recorder(heard)));
    CHECK(heard.messages.empty());
}

TEST_CASE("outside a run, what escapes app code goes on to the caller", "[app-errors]") {
    detail::post_ui_task([] { throw std::runtime_error("no run"); });
    CHECK_THROWS_AS(detail::run_ui_tasks(), std::runtime_error);
}

namespace {

detail::TextSystem& text_system() {
    static detail::TextSystem system;
    return system;
}

// A component whose body starts an effect, then throws.
struct Thrower {
    State<int> watched;
    int* effect_runs;
    [[nodiscard]] View body() const {
        effect([watched = watched, runs = effect_runs] {
            watched();
            ++*runs;
        });
        throw std::runtime_error("body");
    }
};

// A Layout that throws while it measures.
struct ThrowingLayout final : Layout {
    [[nodiscard]] Size size_that_fits(ProposedSize, std::span<const LayoutChild>) const override {
        throw std::runtime_error("layout");
    }
    void place_children(Rect, ProposedSize, std::span<const LayoutChild>) const override {}
};

} // namespace

TEST_CASE("while an App runs, a body that throws is mounted in its place, and what it made goes", "[app-errors]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const Run run;
    const State<int> watched(0);
    int effect_runs = 0;
    const auto root = detail::lower(VStack(Rectangle().frame(10, 10), Thrower{watched, &effect_runs},
                                           Rectangle().frame(10, 10)),
                                    {.text = &text_system()});
    REQUIRE(root);
    const int runs = effect_runs;
    watched = 1;
    detail::flush_effects();
    CHECK(effect_runs == runs); // the effect the body started ended with it
    Heard heard;
    detail::handle_app_errors(recorder(heard));
    CHECK(heard.codes == std::vector<AppCode>{AppCode::body});
    const Size size = root->size_that_fits({100.f, 100.f});
#ifdef NDEBUG
    CHECK(size.width == 10);  // an empty view in its place; the views around it are there
    CHECK(size.height == 28); // the two squares and the stack's spacing around the empty one
#else
    CHECK(size.width > 10); // a placeholder with the message in its place
#endif
}

TEST_CASE("outside a run, a body that throws throws on", "[app-errors]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<int> watched(0);
    int effect_runs = 0;
    CHECK_THROWS_AS(detail::lower(VStack(Thrower{watched, &effect_runs}), {}), std::runtime_error);
}

TEST_CASE("while an App runs, a layout that throws has no size, and a canvas that throws draws nothing", "[app-errors]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const Run run;
    const auto root = detail::lower(VStack({.spacing = 0}, ThrowingLayout()(Rectangle().frame(30, 30)),
                                           Canvas([](GraphicsContext& graphics) {
                                               graphics.fill_rect({0, 0, 5, 5}, Color::black);
                                               throw std::runtime_error("drawing");
                                           }).frame(20, 20)),
                                    {});
    const Size size = root->size_that_fits({100.f, 100.f});
    CHECK(size.height == 20); // the layout's 30 x 30 child got no space
    root->place({}, {100.f, 100.f});
    detail::DisplayList list;
    root->paint(list);
    CHECK(list.commands.empty()); // nor did the canvas's half-drawn rectangle stay
    Heard heard;
    detail::handle_app_errors(recorder(heard));
    CHECK(heard.codes == std::vector<AppCode>{AppCode::layout, AppCode::drawing});
}
