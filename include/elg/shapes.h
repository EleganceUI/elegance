// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_SHAPES_H
#define ELG_SHAPES_H

#include <elg/color.h>
#include <elg/export.h>
#include <elg/geometry.h>
#include <elg/gradient.h>
#include <elg/prop.h>
#include <elg/view.h>

#include <memory>

namespace elg {

namespace detail {
class ShapeNode;
}

// A view that draws a shape filling its frame (taking the whole proposed size, like SwiftUI).
// Shape modifiers return Shape, so they chain until a generic modifier turns it into a View:
//   RoundedRectangle(12).fill(Color::hex(0x8AADF4)).stroke_border(Color::white, 2).padding(8)
// Members are exported individually, like View's (see view.h).
class Shape : public View {
public:
    // A color or gradient, or a closure returning one. Shapes with neither a fill nor a stroke
    // use the environment's foreground.
    ELG_API Shape fill(Prop<ShapeStyle> style) const;
    // A line centered on the shape's edge.
    ELG_API Shape stroke(Prop<Color> color, float line_width = 1) const;
    // A line inside the shape's edge.
    ELG_API Shape stroke_border(Prop<Color> color, float line_width = 1) const;

    // Internal: created by the shape functions below.
    ELG_API explicit Shape(std::shared_ptr<const detail::ShapeNode> node);
};

ELG_API Shape Rectangle();
ELG_API Shape RoundedRectangle(float corner_radius);
ELG_API Shape RoundedRectangle(CornerRadii radii);
// Fully rounded ends along the shorter side.
ELG_API Shape Capsule();
// The largest circle that fits, centered.
ELG_API Shape Circle();

} // namespace elg

#endif // ELG_SHAPES_H
