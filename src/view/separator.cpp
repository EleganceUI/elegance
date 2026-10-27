// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Separator: a line between groups, along either axis.

#include "view/accessibility.h"
#include "view/view_node.h"

#include <elg/shapes.h>
#include <elg/views.h>

#include <algorithm>
#include <memory>
#include <utility>

namespace elg::detail {

namespace {

class SeparatorNode final : public ViewNode {
public:
    SeparatorNode(Axis axis, SeparatorOptions options) : axis_(axis), options_(std::move(options)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        const float thickness = std::max(options_.thickness, 0.f);
        const float inset = options_.inset;
        const Shape line = Rectangle().fill(Prop<ShapeStyle>([color = options_.color](const EnvironmentValues& env) {
            return ShapeStyle(color.get(env));
        }));
        const View body =
            axis_ == Axis::horizontal
                ? line.frame({.max_width = infinity, .min_height = thickness, .max_height = thickness})
                      .padding(EdgeInsets{.leading = inset, .trailing = inset})
                : line.frame({.min_width = thickness, .max_width = thickness, .max_height = infinity})
                      .padding(EdgeInsets{.top = inset, .bottom = inset});
        AccessibilitySpec semantics;
        semantics.role = AccessRole::separator;
        return with_accessibility(detail::lower(body, context), std::move(semantics), context.environment);
    }

private:
    Axis axis_;
    SeparatorOptions options_;
};

} // namespace

} // namespace elg::detail

namespace elg {

View Separator(Axis axis, SeparatorOptions options) {
    return detail::ViewAccess::make(std::make_shared<const detail::SeparatorNode>(axis, std::move(options)));
}

} // namespace elg
