// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// HSplitView / VSplitView, and NavigationSplitView composed from them (design.md §13.5).

#include "view/view_node.h"

#include <elg/animation.h>
#include <elg/navigation.h>
#include <elg/theme.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace elg::detail {

namespace {

constexpr float kLine = 1; // the divider's line, which takes space between the panes
constexpr float kGrab = 4; // how far beside the line a divider can still be grabbed

bool contains(const Rect& rect, Point point) {
    return point.x >= rect.x && point.y >= rect.y && point.x < rect.max_x() && point.y < rect.max_y();
}

class SplitElement;

// A divider: shows the resize cursor while the pointer is over it, and resizes the panes on either
// side when dragged. Its frame is the area it can be grabbed in, around its line.
class SplitDividerElement final : public Element, private EventHandler {
public:
    SplitDividerElement(SplitElement& split, std::size_t index, Axis axis, WindowServices* services,
                        EnvironmentValues environment)
        : split_(&split), index_(index), axis_(axis), services_(services), environment_(std::move(environment)) {}

    EventHandler* event_handler() noexcept override { return this; }
    HoverDelay effective_hover_delay() const override { return {0, 0}; } // the cursor changes at once
    void accessibility(AccessibilityInfo& out) const override { out.hidden = true; }
    void inspect(Inspection& out) const override { out.name = "SplitDivider"; }

protected:
    Size measure(ProposedSize proposal) override { return proposal.replacing_unspecified({}); }
    void arrange(Rect, ProposedSize) override {}
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>&) override { return contains(frame(), point); }

private:
    EventResult pointer(const PointerEvent& event, Rect) override;
    EventResult hover(bool inside) override;
    float along(Point point) const { return axis_ == Axis::horizontal ? point.x : point.y; }
    void show_cursor(bool resize) const {
        if (services_) {
            services_->set_cursor(!resize                     ? CursorShape::arrow
                                  : axis_ == Axis::horizontal ? CursorShape::resize_horizontal
                                                              : CursorShape::resize_vertical);
        }
    }

    SplitElement* split_;
    std::size_t index_; // between laid-out panes index_ and index_ + 1
    Axis axis_;
    WindowServices* services_;
    EnvironmentValues environment_;
    bool hovered_ = false;
    bool dragging_ = false;
    float drag_start_ = 0; // the pointer's position along the axis when the drag began
};

class SplitElement final : public Element {
public:
    SplitElement(Axis axis, std::vector<std::unique_ptr<Element>> children, LowerContext context)
        : axis_(axis), context_(std::move(context)), children_(std::move(children)) {
        for (const auto& child : children_) {
            adopt(*child);
        }
        rebuild_children();
    }

    void inspect(Inspection& out) const override {
        out.name = axis_ == Axis::horizontal ? "HSplitView" : "VSplitView";
        out.detail = std::to_string(laid_out_.size()) + " panes";
    }

    void for_each_child(const std::function<void(Element&)>& visit) override {
        for (const auto& child : children_) {
            visit(*child);
        }
        for (const auto& divider : dividers_) {
            visit(*divider);
        }
    }

    // Where the line after pane `index` is drawn now. When a pane comes or goes, the panes that
    // stay ease to their new frames, and the line goes with them: a pane coming in is already at
    // its frame (its transition moves only its drawing), so the line follows whichever neighbor
    // is still away from where it's laid out.
    float line_position(std::size_t index) const {
        if (index + 1 >= laid_out_.size() || index >= pane_rects_.size()) {
            return 0;
        }
        const Rect before = laid_out_[index]->frame();
        const Rect after = laid_out_[index + 1]->frame();
        const float end = axis_ == Axis::horizontal ? before.max_x() : before.max_y();
        const float start = (axis_ == Axis::horizontal ? after.x : after.y) - kLine;
        const float laid_out = axis_ == Axis::horizontal ? pane_rects_[index].max_x() : pane_rects_[index].max_y();
        return std::abs(end - laid_out) >= std::abs(start - laid_out) ? end : start;
    }

    // Divider drags: the sizes the panes had when it began, then the pointer's offset since.
    void begin_drag();
    void drag(std::size_t divider, float offset);

protected:
    Size measure(ProposedSize proposal) override;
    void arrange(Rect bounds, ProposedSize proposal) override;
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>& path) override;
    void on_structure_changed() override {
        // An If/ForEach pane came or went. Like a layout, this is still one element to its parent.
        rebuild_children();
        invalidate_layout();
    }

private:
    // A pane's size range along the axis, for a length across it.
    struct Limits {
        float min = 0;
        float ideal = 0;
        float max = 0;
    };

    float along(Size size) const { return axis_ == Axis::horizontal ? size.width : size.height; }
    float across(Size size) const { return axis_ == Axis::horizontal ? size.height : size.width; }
    ProposedSize proposal(std::optional<float> along, std::optional<float> across) const {
        return axis_ == Axis::horizontal ? ProposedSize{along, across} : ProposedSize{across, along};
    }
    Limits limits(Element& pane, std::optional<float> across) const;
    float lines() const { return laid_out_.empty() ? 0.f : kLine * static_cast<float>(laid_out_.size() - 1); }
    // The panes' sizes filling `available`: their preferred sizes (new panes at their ideal), with
    // the difference given to the panes that can take it, the highest layout priority first.
    std::vector<float> resolve(float available, std::optional<float> across) const;
    void rebuild_children();

    Axis axis_;
    LowerContext context_;
    std::vector<std::unique_ptr<Element>> children_; // owned, as built
    std::vector<Element*> laid_out_;                 // the panes: If/ForEach flattened
    std::vector<std::unique_ptr<SplitDividerElement>> dividers_;
    // What each pane would like: its ideal size when it appeared, or where it was dragged to. A
    // pane squeezed by a small window gets its size back when the window grows again.
    std::unordered_map<const Element*, float> preferred_;
    std::vector<float> sizes_;     // each pane's size, from the last arrange
    std::vector<Rect> pane_rects_; // where each pane goes, from the last arrange
    std::vector<float> drag_sizes_; // the panes' sizes when a drag began
    float across_ = 0;              // from the last arrange
};

// --- SplitDividerElement ---

EventResult SplitDividerElement::pointer(const PointerEvent& event, Rect) {
    if (event.button != MouseButton::left && event.kind != PointerEvent::Kind::cancel) {
        return {};
    }
    switch (event.kind) {
    case PointerEvent::Kind::down:
        dragging_ = true;
        drag_start_ = along(event.location);
        split_->begin_drag();
        return {.consumed = true, .redraw = true};
    case PointerEvent::Kind::move:
        if (dragging_) {
            split_->drag(index_, along(event.location) - drag_start_);
            return {.consumed = true, .redraw = true};
        }
        return {};
    case PointerEvent::Kind::up:
    case PointerEvent::Kind::cancel:
        dragging_ = false;
        if (!hovered_) {
            show_cursor(false); // released away from the divider
        }
        return {.consumed = true, .redraw = true};
    }
    return {};
}

EventResult SplitDividerElement::hover(bool inside) {
    hovered_ = inside;
    if (inside || !dragging_) { // a fast drag outruns the divider: keep the cursor until it ends
        show_cursor(inside);
    }
    return {.redraw = true};
}

void SplitDividerElement::paint_impl(DisplayList& list) const {
    const Rect bounds = frame();
    // While hovered or dragged, a wider bar in the tint shows what will move.
    const float width = hovered_ || dragging_ ? 3.f : kLine;
    const float middle = split_->line_position(index_) + kLine / 2;
    const Color color = hovered_ || dragging_ ? environment_.get<TintKey>().opacity(dragging_ ? 0.9f : 0.6f)
                                              : environment_.color(ThemeColor::outline);
    const Rect line = axis_ == Axis::horizontal
                          ? Rect{middle - width / 2, bounds.y, width, bounds.height}
                          : Rect{bounds.x, middle - width / 2, bounds.width, width};
    list.commands.push_back(RectCommand{line, {}, ShapeStyle(color), Color::clear, 0});
}

// --- SplitElement ---

void SplitElement::rebuild_children() {
    laid_out_.clear();
    for (const auto& child : children_) {
        child->collect_layout_children(laid_out_);
    }
    // Panes that went keep no size: if one comes back, it starts at its ideal size again.
    std::erase_if(preferred_,
                  [&](const auto& entry) { return std::ranges::find(laid_out_, entry.first) == laid_out_.end(); });

    const std::size_t count = laid_out_.empty() ? 0 : laid_out_.size() - 1;
    if (dividers_.size() != count) {
        while (dividers_.size() > count) {
            dividers_.pop_back();
        }
        while (dividers_.size() < count) {
            auto divider = std::make_unique<SplitDividerElement>(*this, dividers_.size(), axis_, context_.services,
                                                                 context_.environment);
            adopt(*divider);
            dividers_.push_back(std::move(divider));
        }
        if (context_.tree_changed) {
            *context_.tree_changed = true; // the event dispatcher must forget removed dividers
        }
    }
}

SplitElement::Limits SplitElement::limits(Element& pane, std::optional<float> across) const {
    Limits result;
    result.min = std::max(along(pane.size_that_fits(proposal(0.f, across))), 0.f);
    result.max = std::max(along(pane.size_that_fits(proposal(infinity, across))), result.min);
    result.ideal = std::clamp(along(pane.size_that_fits(proposal(std::nullopt, across))), result.min, result.max);
    return result;
}

std::vector<float> SplitElement::resolve(float available, std::optional<float> across) const {
    const std::size_t count = laid_out_.size();
    std::vector<Limits> range(count);
    std::vector<float> sizes(count);
    float total = 0;
    for (std::size_t i = 0; i < count; ++i) {
        range[i] = limits(*laid_out_[i], across);
        const auto preferred = preferred_.find(laid_out_[i]);
        sizes[i] = std::clamp(preferred != preferred_.end() ? preferred->second : range[i].ideal, range[i].min,
                              range[i].max);
        total += sizes[i];
    }

    // Who takes the difference, in order: the highest priority (ties: the last), then the rest
    // from the last pane back.
    std::vector<std::size_t> order(count);
    for (std::size_t i = 0; i < count; ++i) {
        order[i] = count - 1 - i;
    }
    std::ranges::stable_sort(order, std::ranges::greater{},
                             [&](std::size_t i) { return laid_out_[i]->layout_priority(); });

    float difference = available - total;
    for (const std::size_t i : order) {
        if (std::abs(difference) < 0.01f) {
            break;
        }
        const float next = std::clamp(sizes[i] + difference, range[i].min, range[i].max);
        difference -= next - sizes[i];
        sizes[i] = next;
    }
    return sizes;
}

Size SplitElement::measure(ProposedSize proposal) {
    const std::optional<float> along_proposal = axis_ == Axis::horizontal ? proposal.width : proposal.height;
    const std::optional<float> across_proposal = axis_ == Axis::horizontal ? proposal.height : proposal.width;

    // It takes the space it's offered; otherwise its panes' ideal sizes.
    float along_size = 0;
    if (along_proposal && std::isfinite(*along_proposal)) {
        along_size = *along_proposal;
    } else {
        for (Element* pane : laid_out_) {
            along_size += limits(*pane, across_proposal).ideal;
        }
        along_size += lines();
    }
    float across_size = 0;
    if (across_proposal && std::isfinite(*across_proposal)) {
        across_size = *across_proposal;
    } else {
        for (Element* pane : laid_out_) {
            across_size = std::max(across_size, across(pane->size_that_fits(this->proposal(std::nullopt, std::nullopt))));
        }
    }
    return axis_ == Axis::horizontal ? Size{along_size, across_size} : Size{across_size, along_size};
}

void SplitElement::arrange(Rect bounds, ProposedSize) {
    std::vector<Element*> exiting;
    for (const auto& child : children_) {
        child->prune_exiting();
        child->collect_exiting(exiting);
    }
    move_exiting(exiting);
    across_ = across(bounds.size());
    // A pane prefers its ideal size when it appears (later content changes don't resize it), even
    // if it can't have it yet.
    for (Element* pane : laid_out_) {
        if (!preferred_.contains(pane)) {
            preferred_.emplace(pane, limits(*pane, across_).ideal);
        }
    }
    sizes_ = resolve(along(bounds.size()) - lines(), across_);
    const std::vector<float>& sizes = sizes_;
    pane_rects_.clear();
    float position = axis_ == Axis::horizontal ? bounds.x : bounds.y;
    for (std::size_t i = 0; i < laid_out_.size(); ++i) {
        const Rect rect = axis_ == Axis::horizontal ? Rect{position, bounds.y, sizes[i], bounds.height}
                                                    : Rect{bounds.x, position, bounds.width, sizes[i]};
        pane_rects_.push_back(rect);
        laid_out_[i]->place({rect.x, rect.y}, proposal(sizes[i], across_));
        position += sizes[i];
        if (i < dividers_.size()) {
            const float start = position - kGrab; // the grab area, centered on the line
            const Point origin = axis_ == Axis::horizontal ? Point{start, bounds.y} : Point{bounds.x, start};
            dividers_[i]->place(origin, proposal(kLine + 2 * kGrab, across_));
            position += kLine;
        }
    }
}

void SplitElement::paint_impl(DisplayList& list) const {
    // Each pane is clipped to its share, so content bigger than its pane doesn't run into the next.
    // Along the axis the clip follows the pane's frame, which eases when a pane comes or goes.
    for (std::size_t i = 0; i < laid_out_.size() && i < pane_rects_.size(); ++i) {
        const Rect pane = laid_out_[i]->frame();
        const Rect share = pane_rects_[i];
        const Rect clip = axis_ == Axis::horizontal ? Rect{pane.x, share.y, pane.width, share.height}
                                                    : Rect{share.x, pane.y, share.width, pane.height};
        list.commands.push_back(PushClipCommand{clip, {}});
        laid_out_[i]->paint(list);
        list.commands.push_back(PopClipCommand{});
    }
    // Panes that went, animating out where they last were.
    std::vector<Element*> exiting;
    for (const auto& child : children_) {
        child->collect_exiting(exiting);
    }
    for (const Element* element : exiting) {
        element->paint(list);
    }
    for (const auto& divider : dividers_) {
        divider->paint(list);
    }
}

bool SplitElement::hit_impl(Point point, std::vector<Element*>& path) {
    // Dividers first: they reach a little into the panes beside them.
    for (const auto& divider : dividers_) {
        if (divider->hit_test(point, path)) {
            return true;
        }
    }
    for (std::size_t i = 0; i < laid_out_.size() && i < pane_rects_.size(); ++i) {
        if (contains(pane_rects_[i], point) && laid_out_[i]->hit_test(point, path)) {
            return true;
        }
    }
    return false;
}

void SplitElement::begin_drag() {
    drag_sizes_ = sizes_;
}

void SplitElement::drag(std::size_t divider, float offset) {
    if (divider + 1 >= laid_out_.size() || drag_sizes_.size() != laid_out_.size()) {
        return;
    }
    // Only the two panes beside the divider change, each within its limits.
    const Limits first = limits(*laid_out_[divider], across_);
    const Limits second = limits(*laid_out_[divider + 1], across_);
    const float a = drag_sizes_[divider];
    const float b = drag_sizes_[divider + 1];
    const float lowest = std::max(first.min - a, b - second.max);
    const float highest = std::min(first.max - a, b - second.min);
    const float delta = lowest <= highest ? std::clamp(offset, lowest, highest) : 0.f;
    preferred_[laid_out_[divider]] = a + delta;
    preferred_[laid_out_[divider + 1]] = b - delta;
    invalidate_layout();
    if (context_.needs_frame) {
        *context_.needs_frame = true;
    }
}

class SplitNode final : public ViewNode {
public:
    SplitNode(Axis axis, std::vector<View> panes) : axis_(axis), panes_(std::move(panes)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        const LowerContext pane_context = context.nested();
        std::vector<std::unique_ptr<Element>> children;
        children.reserve(panes_.size());
        for (const View& pane : panes_) {
            children.push_back(detail::lower(pane, pane_context));
        }
        return std::make_unique<SplitElement>(axis_, std::move(children), pane_context);
    }

private:
    Axis axis_;
    std::vector<View> panes_;
};

// A NavigationSplitView column: its width range, the full height, content at the top.
View column(View content, ColumnWidth width) {
    return std::move(content).frame({.min_width = width.min,
                                     .ideal_width = width.ideal,
                                     .max_width = width.max,
                                     .max_height = infinity,
                                     .alignment = Alignment::top_leading});
}

View sidebar_column(View sidebar, const NavigationSplitViewOptions& options) {
    View result = column(std::move(sidebar), options.sidebar_width).background(ThemeColor::surface);
    if (!options.sidebar_visible) {
        return result;
    }
    // Hidden, it slides out to the leading edge and the other columns take its width.
    const Binding<bool> visible = *options.sidebar_visible;
    return If([visible] { return visible(); }, result.transition(Transition::move(Edges::leading)));
}

View detail_column(View detail) {
    // The rest of the width: it takes size changes first.
    return std::move(detail).frame({.min_width = 200, .max_width = infinity, .max_height = infinity}).layout_priority(1);
}

} // namespace

} // namespace elg::detail

namespace elg {

using detail::ViewAccess;

View detail::make_split_view(Axis axis, std::vector<View> panes) {
    return ViewAccess::make(std::make_shared<const SplitNode>(axis, std::move(panes)));
}

View NavigationSplitView(View sidebar, View detail, NavigationSplitViewOptions options) {
    return HSplitView(detail::sidebar_column(std::move(sidebar), options), detail::detail_column(std::move(detail)));
}

View NavigationSplitView(View sidebar, View content, View detail, NavigationSplitViewOptions options) {
    return HSplitView(detail::sidebar_column(std::move(sidebar), options),
                      detail::column(std::move(content), options.content_width),
                      detail::detail_column(std::move(detail)));
}

} // namespace elg
