// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// LazyVStack / LazyHStack (design.md §7): a stack over a ForEach that builds only the items near
// the visible part of its ScrollView.

#include "view/view_node.h"

#include <elg/error.h>
#include <elg/shapes.h>
#include <elg/views.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace elg::detail {

namespace {

constexpr float kDefaultEstimate = 44; // main-axis size assumed for items never built

// StackSeparators, with the color resolved.
struct Separators {
    float inset = 0;
    float thickness = 1;
};

class LazyStackElement final : public Element {
public:
    LazyStackElement(Axis axis, float alignment, float spacing, LowerContext context,
                     std::optional<Separators> separators)
        : axis_(axis), alignment_(alignment), spacing_(spacing), context_(std::move(context)),
          separators_(separators) {}

    void set_separator_color(Color color) { separator_color_ = color; }
    bool always_arranges() const override { return true; } // builds what scrolled into view

    // Takes the ForEach's items. Built items whose key is gone or whose value changed are
    // destroyed (and rebuilt by the next layout if still in view).
    void set_items(std::vector<ForEachItem> items) {
        std::map<ItemKey, const ForEachItem*> by_key;
        for (const ForEachItem& item : items) {
            by_key.emplace(item.key, &item);
        }
        bool removed = false;
        for (auto it = rows_.begin(); it != rows_.end();) {
            const auto found = by_key.find(it->first);
            if (found == by_key.end() || !found->second->equals(found->second->value.get(), it->second.value.get())) {
                if (found == by_key.end()) {
                    sizes_.erase(it->first);
                }
                it = rows_.erase(it);
                removed = true;
            } else {
                ++it;
            }
        }
        items_ = std::move(items);
        // Known sizes by position, so layout doesn't look every item up by key.
        known_.assign(items_.size(), -1.f);
        known_sum_ = 0;
        known_count_ = 0;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            if (const auto size = sizes_.find(items_[i].key); size != sizes_.end()) {
                known_[i] = size->second;
                known_sum_ += static_cast<double>(size->second);
                ++known_count_;
            }
        }
        shown_.clear(); // rebuilt by the next layout; nothing may point at destroyed rows
        if (removed && context_.tree_changed) {
            *context_.tree_changed = true;
        }
        invalidate_layout();
        if (context_.needs_frame) {
            *context_.needs_frame = true;
        }
    }

    void for_each_child(const std::function<void(Element&)>& visit) override {
        for (auto& [key, row] : rows_) {
            visit(*row.element);
        }
    }

    std::optional<float> item_offset(const ItemKey& key) const override {
        const float estimate = estimated_size();
        double position = 0;
        for (std::size_t i = 0; i < items_.size(); ++i) {
            if (items_[i].key == key) {
                return static_cast<float>(position);
            }
            position += static_cast<double>(size_at(i, estimate) + spacing_);
        }
        return std::nullopt;
    }

protected:
    Size measure(ProposedSize proposal) override {
        const std::optional<float> cross = cross_of(proposal);
        const float estimate = estimated_size();
        // In double: summed in float, ten thousand rows drift by a row's height.
        double main = known_sum_ + static_cast<double>(estimate) * static_cast<double>(items_.size() - known_count_);
        if (items_.size() > 1) {
            main += static_cast<double>(spacing_) * static_cast<double>(items_.size() - 1);
        }
        // Across: what's offered, else the widest item built so far.
        float cross_size = 0;
        if (cross && std::isfinite(*cross)) {
            cross_size = *cross;
        } else {
            for (const auto& [key, row] : rows_) {
                cross_size = std::max(cross_size, cross_of(row.element->size_that_fits(item_proposal(cross))));
            }
        }
        const auto length = static_cast<float>(main);
        return axis_ == Axis::vertical ? Size{cross_size, length} : Size{length, cross_size};
    }

    void arrange(Rect bounds, ProposedSize) override;

    void paint_impl(DisplayList& list) const override {
        for (const Shown& shown : shown_) {
            shown.element->paint(list);
        }
        if (!separators_) {
            return;
        }
        // In the middle of the spacing after each item but the last, across the stack.
        const Rect bounds = frame();
        const float inset = separators_->inset;
        const float thickness = separators_->thickness;
        for (const Shown& shown : shown_) {
            if (shown.index + 1 >= items_.size()) {
                continue;
            }
            const Rect item = shown.element->frame();
            const Rect line = axis_ == Axis::vertical
                                  ? Rect{bounds.x + inset, item.max_y() + spacing_ / 2 - thickness / 2,
                                         bounds.width - inset, thickness}
                                  : Rect{item.max_x() + spacing_ / 2 - thickness / 2, bounds.y + inset, thickness,
                                         bounds.height - inset};
            list.commands.push_back(RectCommand{line, {}, ShapeStyle(separator_color_), Color::clear, 0});
        }
    }

    bool hit_impl(Point point, std::vector<Element*>& path) override {
        for (auto it = shown_.rbegin(); it != shown_.rend(); ++it) {
            if (it->element->hit_test(point, path)) {
                return true;
            }
        }
        return false;
    }

private:
    // Declaration order: the scope is destroyed before the element, so no effect runs against a
    // destroyed element.
    struct Row {
        std::shared_ptr<const void> value;
        std::unique_ptr<Element> element;
        std::unique_ptr<Scope> scope;
    };
    struct Shown {
        Element* element;
        std::size_t index;
    };

    float main_of(Size size) const { return axis_ == Axis::vertical ? size.height : size.width; }
    float cross_of(Size size) const { return axis_ == Axis::vertical ? size.width : size.height; }
    std::optional<float> cross_of(ProposedSize proposal) const {
        return axis_ == Axis::vertical ? proposal.width : proposal.height;
    }
    // Items get the cross size and are free along the main axis.
    ProposedSize item_proposal(std::optional<float> cross) const {
        return axis_ == Axis::vertical ? ProposedSize{cross, std::nullopt} : ProposedSize{std::nullopt, cross};
    }

    // The average of the sizes measured so far.
    float estimated_size() const {
        return known_count_ == 0 ? kDefaultEstimate : static_cast<float>(known_sum_ / static_cast<double>(known_count_));
    }
    float size_at(std::size_t index, float estimate) const {
        return known_[index] >= 0 ? known_[index] : estimate;
    }
    void set_size(std::size_t index, float size) {
        if (known_[index] >= 0) {
            known_sum_ -= static_cast<double>(known_[index]);
        } else {
            ++known_count_;
        }
        known_[index] = size;
        known_sum_ += static_cast<double>(size);
        sizes_[items_[index].key] = size;
    }

    Row mount(const ForEachItem& item) {
        Row row{item.value, nullptr, std::make_unique<Scope>()};
        Scope::Use use(*row.scope);
        begin_untracked(); // building an item must not subscribe whatever is running
        try {
            row.element = lower(item.view(), context_.nested());
        } catch (...) {
            end_untracked();
            throw;
        }
        end_untracked();
        adopt(*row.element);
        return row;
    }

    Axis axis_;
    float alignment_; // 0: leading/top, 0.5: center, 1: trailing/bottom
    float spacing_;
    LowerContext context_;
    std::optional<Separators> separators_;
    Color separator_color_;
    std::vector<ForEachItem> items_;
    std::map<ItemKey, Row> rows_;    // built items
    std::map<ItemKey, float> sizes_; // measured main-axis sizes, by key (kept across item changes)
    std::vector<float> known_;       // the same by item position; -1 when not measured yet
    double known_sum_ = 0; // double: sums of thousands of sizes
    std::size_t known_count_ = 0;
    std::vector<Shown> shown_;       // built items in order, as last laid out
    // Scroll anchoring: the first row in view at the last layout, and where it was then.
    struct Anchor {
        ItemKey key;
        float position;
    };
    std::optional<Anchor> anchor_;
};

void LazyStackElement::arrange(Rect bounds, ProposedSize) {
    const bool vertical = axis_ == Axis::vertical;
    const float cross = vertical ? bounds.width : bounds.height;
    const ProposedSize proposal = item_proposal(cross);

    // The visible stretch along the main axis (local coordinates), with half a viewport of
    // margin each way so items exist before they scroll in.
    double low = -std::numeric_limits<double>::infinity(); // double, as positions are
    double high = std::numeric_limits<double>::infinity();
    std::optional<float> visible_start; // inside a scroll view: where its visible part begins
    for (const Element* ancestor = parent(); ancestor; ancestor = ancestor->parent()) {
        if (const std::optional<Rect> visible = ancestor->visible_rect()) {
            const float start = vertical ? visible->y - bounds.y : visible->x - bounds.x;
            const float length = vertical ? visible->height : visible->width;
            low = static_cast<double>(start - length / 2);
            high = static_cast<double>(start + length * 1.5f);
            visible_start = start;
            break;
        }
    }

    const float estimate = estimated_size();
    struct Placement {
        Element* element;
        float position; // along the main axis, in the stack
        float size;
        float cross_offset;
    };
    std::vector<Placement> placements;
    std::optional<float> anchor_now; // where the anchor row is now
    std::map<ItemKey, Row> kept;
    bool structure_changed = false;
    bool size_changed = false;
    shown_.clear();
    double position = 0; // double, as in measure()
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const ForEachItem& item = items_[i];
        float size = size_at(i, estimate);
        if (position + static_cast<double>(size) >= low && position <= high) {
            auto existing = rows_.find(item.key);
            Row row;
            if (existing != rows_.end()) {
                row = std::move(existing->second);
                rows_.erase(existing);
            } else {
                row = mount(item);
                structure_changed = true;
            }
            const Size actual = row.element->size_that_fits(proposal);
            if (known_[i] < 0 || std::abs(known_[i] - main_of(actual)) > 0.01f) {
                set_size(i, main_of(actual));
                size_changed = true;
            }
            size = main_of(actual);
            placements.push_back({row.element.get(), static_cast<float>(position), size,
                                  (cross - cross_of(actual)) * alignment_});
            shown_.push_back({row.element.get(), i});
            kept.emplace(item.key, std::move(row));
        }
        if (anchor_ && item.key == anchor_->key) {
            anchor_now = static_cast<float>(position);
        }
        position += static_cast<double>(size + spacing_);
    }

    // Scroll anchoring: rows above the one at the top of the view changed size (estimates became
    // real sizes, items were inserted): move the scroll offset by as much, and place everything
    // that much earlier now, so nothing on screen jumps.
    float shift = 0;
    if (anchor_ && anchor_now && *anchor_now != anchor_->position) {
        const float delta = *anchor_now - anchor_->position;
        for (Element* ancestor = parent(); ancestor; ancestor = ancestor->parent()) {
            if (ancestor->visible_rect()) {
                if (ancestor->anchor_shift(axis_, delta)) {
                    shift = delta;
                }
                break;
            }
        }
    }
    anchor_.reset();
    for (const Placement& placed : placements) {
        const float at = placed.position - shift;
        placed.element->place(vertical ? Point{bounds.x + placed.cross_offset, bounds.y + at}
                                       : Point{bounds.x + at, bounds.y + placed.cross_offset},
                              proposal);
        // The new anchor: the first row reaching into the visible part.
        if (!anchor_ && visible_start && placed.position + placed.size > *visible_start + shift) {
            for (const Shown& shown : shown_) {
                if (shown.element == placed.element) {
                    anchor_ = Anchor{items_[shown.index].key, placed.position};
                    break;
                }
            }
        }
    }

    // What's left was scrolled out of reach: destroy it.
    if (!rows_.empty()) {
        structure_changed = true;
    }
    rows_ = std::move(kept);

    if (structure_changed && context_.tree_changed) {
        *context_.tree_changed = true;
    }
    // Real sizes replaced estimates: the total changed, so lay out again next frame.
    if (size_changed) {
        invalidate_layout();
        if (context_.needs_frame) {
            *context_.needs_frame = true;
        }
    }
}

class LazyStackNode final : public ViewNode {
public:
    LazyStackNode(Axis axis, float alignment, float spacing, std::function<std::vector<ForEachItem>()> items,
                  std::optional<StackSeparators> separators)
        : axis_(axis), alignment_(alignment), spacing_(spacing), items_(std::move(items)),
          separators_(std::move(separators)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        std::optional<Separators> separators;
        if (separators_) {
            separators = Separators{separators_->inset, separators_->thickness};
        }
        std::function<std::vector<ForEachItem>()> items = items_;
        if (auto selection = claim_selection(context, axis_)) {
            items = selectable_items(std::move(items), std::move(selection));
        }
        LowerContext inner = context;
        inner.selection.reset();
        auto element = std::make_unique<LazyStackElement>(axis_, alignment_, spacing_, std::move(inner), separators);
        LazyStackElement* const target = element.get();
        if (separators_) {
            detail::bind(separators_->color, context, [target](Color color) { target->set_separator_color(color); });
        }
        // Reading the items subscribes this effect; building items happens in layout.
        effect([target, items = std::move(items)] { target->set_items(items()); });
        return element;
    }

private:
    Axis axis_;
    float alignment_;
    float spacing_;
    std::function<std::vector<ForEachItem>()> items_;
    std::optional<StackSeparators> separators_;
};

const std::function<std::vector<ForEachItem>()>& items_of(const View& for_each, std::string_view who) {
    const auto node = std::dynamic_pointer_cast<const ForEachNode>(ViewAccess::node(for_each));
    if (!node) {
        throw UsageError(std::string(who) + ": the content must be a ForEach");
    }
    return node->items();
}

float fraction(HorizontalAlignment alignment) {
    return alignment == HorizontalAlignment::leading ? 0.f : alignment == HorizontalAlignment::center ? 0.5f : 1.f;
}

float fraction(VerticalAlignment alignment) {
    return alignment == VerticalAlignment::top ? 0.f : alignment == VerticalAlignment::center ? 0.5f : 1.f;
}

} // namespace

} // namespace elg::detail

namespace elg {

using detail::ViewAccess;

View LazyVStack(LazyVStackOptions options, View for_each) {
    return ViewAccess::make(std::make_shared<const detail::LazyStackNode>(
        Axis::vertical, detail::fraction(options.alignment), options.spacing.value_or(default_stack_spacing),
        detail::items_of(for_each, "LazyVStack"), std::move(options.separators)));
}

View LazyVStack(View for_each) {
    return LazyVStack(LazyVStackOptions{}, std::move(for_each));
}

View LazyHStack(LazyHStackOptions options, View for_each) {
    return ViewAccess::make(std::make_shared<const detail::LazyStackNode>(
        Axis::horizontal, detail::fraction(options.alignment), options.spacing.value_or(default_stack_spacing),
        detail::items_of(for_each, "LazyHStack"), std::move(options.separators)));
}

View LazyHStack(View for_each) {
    return LazyHStack(LazyHStackOptions{}, std::move(for_each));
}

} // namespace elg
