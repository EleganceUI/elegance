// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include <elg/shapes.h>

#include "view/view_node.h"

namespace elg {

using detail::ShapeKind;
using detail::ShapeNode;
using detail::ShapeSpec;

namespace {

Shape make_shape(ShapeSpec spec) {
    return Shape(std::make_shared<const ShapeNode>(std::move(spec)));
}

ShapeSpec spec_of(const Shape& shape) {
    // A Shape is only ever constructed from a ShapeNode.
    return static_cast<const ShapeNode&>(*detail::ViewAccess::node(shape)).spec();
}

} // namespace

Shape::Shape(std::shared_ptr<const ShapeNode> node) : View(detail::ViewAccess::make(std::move(node))) {}

Shape Shape::fill(Prop<ShapeStyle> style) const {
    ShapeSpec spec = spec_of(*this);
    spec.fill = std::move(style);
    return make_shape(std::move(spec));
}

Shape Shape::stroke(Color color, float line_width) const {
    ShapeSpec spec = spec_of(*this);
    spec.stroke = detail::ShapeStroke{color, line_width, true};
    return make_shape(std::move(spec));
}

Shape Shape::stroke_border(Color color, float line_width) const {
    ShapeSpec spec = spec_of(*this);
    spec.stroke = detail::ShapeStroke{color, line_width, false};
    return make_shape(std::move(spec));
}

Shape Rectangle() {
    return make_shape({ShapeKind::rectangle});
}

Shape RoundedRectangle(float corner_radius) {
    return make_shape({ShapeKind::rounded_rectangle, CornerRadii::all(corner_radius)});
}

Shape RoundedRectangle(CornerRadii radii) {
    return make_shape({ShapeKind::rounded_rectangle, radii});
}

Shape Capsule() {
    return make_shape({ShapeKind::capsule});
}

Shape Circle() {
    return make_shape({ShapeKind::circle});
}

} // namespace elg
