// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include <elg/graphics_context.h>

#include "render/display_list.h"

#include <algorithm>

namespace elg {

GraphicsContext::GraphicsContext(detail::DisplayList& list, Rect bounds, float scale) noexcept
    : list_(&list), bounds_(bounds), scale_(scale) {}

Size GraphicsContext::size() const noexcept {
    return bounds_.size();
}

float GraphicsContext::scale() const noexcept {
    return scale_;
}

void GraphicsContext::fill_rect(Rect rect, Color color, CornerRadii radii) {
    draw_rect(rect, radii, color, Color::clear, 0);
}

void GraphicsContext::stroke_rect(Rect rect, Color color, float width, CornerRadii radii) {
    draw_rect(rect, radii, Color::clear, color, width);
}

void GraphicsContext::draw_rect(Rect rect, CornerRadii radii, Color fill, Color border, float border_width) {
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    rect.x += bounds_.x;
    rect.y += bounds_.y;
    list_->commands.push_back(detail::RectCommand{rect, radii, fill, border, std::max(0.f, border_width)});
}

} // namespace elg
