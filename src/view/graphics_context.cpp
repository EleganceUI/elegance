// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include <elg/graphics_context.h>

#include "display/display_list.h"
#include "view/path_cache.h"
#include "view/animated.h"
#include "view/element.h"

#include <algorithm>

namespace elg {

GraphicsContext::GraphicsContext(detail::DisplayList& list, Rect bounds, float scale) noexcept
    : list_(&list), bounds_(bounds), scale_(scale) {}

GraphicsContext::~GraphicsContext() {
    while (clips_ > 0) {
        pop_clip();
    }
}

void GraphicsContext::push_clip(Rect rect, CornerRadii radii) {
    rect.x += bounds_.x;
    rect.y += bounds_.y;
    list_->commands.emplace_back(detail::PushClipCommand{rect, radii});
    ++clips_;
}

void GraphicsContext::pop_clip() {
    if (clips_ == 0) {
        return;
    }
    list_->commands.emplace_back(detail::PopClipCommand{});
    --clips_;
}

Size GraphicsContext::size() const noexcept {
    return bounds_.size();
}

float GraphicsContext::scale() const noexcept {
    return scale_;
}

double GraphicsContext::time() noexcept {
    detail::paint_changes_over_time(); // what it draws with it isn't kept for the next frame
    return detail::frame_time();
}

void GraphicsContext::request_frame() noexcept {
    list_->animating = true;
}

void GraphicsContext::fill_rect(Rect rect, Color color, CornerRadii radii) {
    draw_rect(rect, radii, color, Color::clear, 0);
}

void GraphicsContext::stroke_rect(Rect rect, Color color, float width, CornerRadii radii) {
    draw_rect(rect, radii, Color::clear, color, width);
}

namespace {

// `path`, or with `stroke` its stroke's outline, flattened as finely as `scale` needs: from the
// paint's PathCache when there is one.
detail::PathCache::Flattened flattened(const Path& path, const StrokeStyle* stroke, float scale) {
    const float tolerance = 0.25f / std::max(scale, 0.25f);
    detail::PathCache* const cache = detail::PathCache::current();
    return cache ? cache->get(path, stroke, tolerance) : detail::PathCache::flatten(path, stroke, tolerance);
}

void add_path(detail::DisplayList& list, Rect frame, detail::PathCache::Flattened shape, ShapeStyle style,
              FillRule rule) {
    if (shape.polygons->empty()) {
        return;
    }
    const Rect& bounds = shape.bounds;
    const Rect paint_frame{frame.x + bounds.x, frame.y + bounds.y, bounds.width, bounds.height};
    list.commands.emplace_back(detail::PathCommand{std::move(shape.polygons), bounds, 1, {frame.x, frame.y},
                                                   paint_frame, std::move(style), rule});
}

} // namespace

void GraphicsContext::fill_path(const Path& path, ShapeStyle style, FillRule rule) {
    if (list_->probe) {
        return;
    }
    add_path(*list_, bounds_, flattened(path, nullptr, scale_), std::move(style), rule);
}

void GraphicsContext::stroke_path(const Path& path, Color color, const StrokeStyle& style) {
    if (list_->probe) {
        return;
    }
    add_path(*list_, bounds_, flattened(path, &style, scale_), ShapeStyle(color), FillRule::non_zero);
}

void GraphicsContext::draw_rect(Rect rect, CornerRadii radii, Color fill, Color border, float border_width) {
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    rect.x += bounds_.x;
    rect.y += bounds_.y;
    list_->commands.emplace_back(detail::RectCommand{rect, radii, fill, border, std::max(0.f, border_width)});
}

} // namespace elg
