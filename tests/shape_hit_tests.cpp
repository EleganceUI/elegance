// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Hit testing a shape: as a fill, a clip_shape or a content_shape, it takes the same points.

#include "view/view_node.h"

#include <elg/path.h>
#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace elg;

namespace {

bool hits(const View& view, Point point) {
    const auto root = detail::lower(view, {});
    root->size_that_fits({100.f, 100.f});
    root->place({}, {100.f, 100.f});
    std::vector<detail::Element*> path;
    return root->hit_test(point, path);
}

// A triangle pointing up: its box's top corners are outside it.
Shape Triangle() {
    return CustomShape([](Rect r) {
        return Path().move_to({r.x + r.width / 2, r.y}).line_to({r.max_x(), r.max_y()}).line_to({r.x, r.max_y()}).close();
    });
}

const View kSquare = Rectangle().fill(Color::white).frame(100, 100);

} // namespace

TEST_CASE("a path shape takes the same points as a fill, a clip and a content shape", "[hit-test]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const Point inside{50, 70};
    const Point corner{5, 5}; // in the box, outside the triangle
    for (const View& view : {View(Triangle().fill(Color::white).frame(100, 100)), kSquare.clip_shape(Triangle()),
                             kSquare.content_shape(Triangle())}) {
        CHECK(hits(view, inside));
        CHECK_FALSE(hits(view, corner));
    }
}

TEST_CASE("rounded shapes leave their corners out everywhere", "[hit-test]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    for (const View& view : {View(Circle().fill(Color::white).frame(100, 100)), kSquare.clip_shape(Circle()),
                             kSquare.content_shape(Circle())}) {
        CHECK(hits(view, {50, 50}));
        CHECK_FALSE(hits(view, {3, 3}));
    }
}

TEST_CASE("a filled shape with a dashed stroke is hit inside, not only on the dashes", "[hit-test]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const View dashed = Rectangle().fill(Color::white).stroke(Color::black, StrokeStyle{.width = 2, .dash = {4, 4}}).frame(100, 100);
    CHECK(hits(dashed, {50, 50}));
}

TEST_CASE("a button takes its whole frame unless a content shape narrows it", "[hit-test]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const View label = Rectangle().fill(Color::white).frame(100, 100);
    CHECK(hits(Button(label, [] {}).button_style(PlainButtonStyle{}), {2, 2}));
    CHECK_FALSE(hits(Button(label, [] {}).button_style(PlainButtonStyle{}).content_shape(Capsule()), {2, 2}));
}
