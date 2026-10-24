// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include <elg/graphics_context.h>

#include "render/display_list.h"
#include "view/animated.h"

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
    list_->commands.push_back(detail::PushClipCommand{rect, radii});
    ++clips_;
}

void GraphicsContext::pop_clip() {
    if (clips_ == 0) {
        return;
    }
    list_->commands.push_back(detail::PopClipCommand{});
    --clips_;
}

Size GraphicsContext::size() const noexcept {
    return bounds_.size();
}

float GraphicsContext::scale() const noexcept {
    return scale_;
}

double GraphicsContext::time() const noexcept {
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

std::shared_ptr<const std::vector<std::vector<Point>>> closed_polygons(const Path& path, float tolerance, Rect& bounds) {
    auto polygons = std::make_shared<std::vector<std::vector<Point>>>();
    bool any = false;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    for (Path::Polyline& line : path.flatten(tolerance)) {
        if (line.points.size() < 3) {
            continue;
        }
        for (const Point p : line.points) {
            x0 = any ? std::min(x0, p.x) : p.x;
            y0 = any ? std::min(y0, p.y) : p.y;
            x1 = any ? std::max(x1, p.x) : p.x;
            y1 = any ? std::max(y1, p.y) : p.y;
            any = true;
        }
        polygons->push_back(std::move(line.points));
    }
    bounds = {x0, y0, x1 - x0, y1 - y0};
    return polygons;
}

} // namespace

void GraphicsContext::fill_path(const Path& path, ShapeStyle style, FillRule rule) {
    Rect bounds;
    auto polygons = closed_polygons(path, 0.25f / std::max(scale_, 0.25f), bounds);
    if (polygons->empty()) {
        return;
    }
    list_->commands.push_back(detail::PathCommand{std::move(polygons), bounds, 1, {bounds_.x, bounds_.y},
                                                  {bounds_.x + bounds.x, bounds_.y + bounds.y, bounds.width, bounds.height},
                                                  std::move(style), rule});
}

void GraphicsContext::stroke_path(const Path& path, Color color, const StrokeStyle& style) {
    const float tolerance = 0.25f / std::max(scale_, 0.25f);
    fill_path(path.stroked(style, tolerance), ShapeStyle(color), FillRule::non_zero);
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
