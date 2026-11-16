// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Floats that become integers: NaN, infinities and values out of the integer's range, where the
// float comes from geometry (sizes, pointer locations, path data). Converting those directly is
// undefined behavior, which the sanitizer builds stop on.

#include "core/numeric.h"
#include "text/text_system.h"
#include "view/text_field.h"
#include "view/view_node.h"

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

using namespace elg;
using detail::clamped_cast;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInfinity = std::numeric_limits<float>::infinity();

detail::TextSystem& text_system() {
    static detail::TextSystem system;
    return system;
}

} // namespace

TEST_CASE("clamped_cast maps NaN to zero and clamps to the integer's range", "[numeric]") {
    CHECK(clamped_cast<int>(kNaN) == 0);
    CHECK(clamped_cast<int>(kInfinity) == std::numeric_limits<int>::max());
    CHECK(clamped_cast<int>(-kInfinity) == std::numeric_limits<int>::min());
    CHECK(clamped_cast<int>(-2.7f) == -2); // toward zero, as a conversion
    CHECK(clamped_cast<int>(1e20f, 1, 10) == 10);
    CHECK(clamped_cast<int>(kNaN, 1, 10) == 1); // zero, then clamped
    CHECK(clamped_cast<std::uint32_t>(-1.0) == 0);
    CHECK(clamped_cast<std::uint32_t>(5e9) == std::numeric_limits<std::uint32_t>::max());

    // 64-bit integers, whose bounds a double can't hold exactly.
    CHECK(clamped_cast<std::size_t>(kNaN) == 0);
    CHECK(clamped_cast<std::size_t>(-1e30) == 0);
    CHECK(clamped_cast<std::size_t>(1e30) == std::numeric_limits<std::size_t>::max());
    CHECK(clamped_cast<std::size_t>(18446744073709551615.0) == std::numeric_limits<std::size_t>::max());
    CHECK(clamped_cast<std::int64_t>(9223372036854775807.0) == std::numeric_limits<std::int64_t>::max());
    CHECK(clamped_cast<std::int64_t>(-9223372036854775808.0) == std::numeric_limits<std::int64_t>::min());
    CHECK(clamped_cast<std::int64_t>(9007199254740993.0) == 9007199254740992); // as the double holds it
    CHECK(clamped_cast<std::size_t>(12.9, 0, 3) == 3);
}

TEST_CASE("an arc with a radius that isn't a number builds and flattens", "[numeric][path]") {
    Path arc;
    arc.move_to({0, 0}).arc_to(kNaN, 5, 0, false, true, {10, 0});
    CHECK_NOTHROW(arc.flatten());
    Path huge;
    huge.move_to({0, 0}).arc_to(5, kInfinity, 0, true, false, {10, 0});
    CHECK_NOTHROW(huge.flatten());
}

TEST_CASE("text laid out in a height out of any range shows at least a line", "[numeric][text]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const std::unique_ptr<detail::Element> text = detail::lower(Text("one\ntwo"), {.text = &text_system()});
    const float one_line = text->size_that_fits({200.f, 1.f}).height;
    CHECK(one_line > 0);
    CHECK(text->size_that_fits({200.f, 1e30f}).height > one_line); // more lines than a size_t can count
    CHECK(text->size_that_fits({200.f, -1e30f}).height == one_line);
}

TEST_CASE("a tap far outside a styled text's lines finds no run", "[numeric][text]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    int taps = 0;
    const TextRun run{.text = "tap", .on_tap = [&] { ++taps; }};
    const std::unique_ptr<detail::Element> text = detail::lower(StyledText({run}), {.text = &text_system()});
    text->size_that_fits({200.f, 100.f});
    text->place({}, {200.f, 100.f});
    EventHandler* handler = text->event_handler();
    REQUIRE(handler != nullptr);
    for (const float y : {1e30f, kInfinity}) {
        const PointerEvent down{.kind = PointerEvent::Kind::down, .location = {1, y}};
        CHECK_FALSE(handler->pointer(down, text->frame()).consumed);
    }
    // NaN counts as 0: the first line, where the run is.
    CHECK(handler->pointer({.kind = PointerEvent::Kind::down, .location = {1, kNaN}}, text->frame()).consumed);
    CHECK(taps == 0);
}

TEST_CASE("a click at a location that isn't a number puts the caret on a line", "[numeric][text-input]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const State<std::string> value(std::string("one\ntwo"));
    detail::TextEditor editor(text_system(), value.binding(), {}, nullptr, nullptr, {}, true);
    editor.set_font(Font::system(15));
    CHECK(editor.measure({200.f, 100.f}).height > 0);
    editor.arranged({0, 0, 200, 100});
    for (const float y : {kNaN, 1e30f, -kInfinity}) {
        const PointerEvent down{.kind = PointerEvent::Kind::down, .location = {5, y}};
        CHECK(editor.pointer(down).consumed);
        editor.pointer({.kind = PointerEvent::Kind::up, .location = {5, 0}});
    }
}

TEST_CASE("a progress ring with a fraction that isn't a number draws", "[numeric][progress]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const std::unique_ptr<detail::Element> ring = detail::lower(Progress(kNaN).progress_style(CircularProgressStyle{}), {});
    ring->size_that_fits({100.f, 100.f});
    ring->place({}, {100.f, 100.f});
    detail::DisplayList list;
    CHECK_NOTHROW(ring->paint(list));
}
