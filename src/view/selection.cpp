// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// View::selection: the rows of a stack over a ForEach, selectable by key with the pointer, the
// keyboard and assistive technology. The stack's ForEach wraps each item in a row
// (selectable_items); the element around the stack takes focus and keys for all of them.

#include "view/view_node.h"

#include "core/log.h"

#include <elg/environment.h>
#include <elg/styles.h>

#include <algorithm>
#include <unordered_map>

namespace elg::detail {

namespace {

bool inside(const Rect& rect, Point point) {
    return point.x >= rect.x && point.x < rect.max_x() && point.y >= rect.y && point.y < rect.max_y();
}

// The scroll view `element` is in, if any.
Element* scroll_container(Element& element) {
    for (Element* ancestor = element.parent(); ancestor; ancestor = ancestor->parent()) {
        if (ancestor->visible_rect()) {
            return ancestor;
        }
    }
    return nullptr;
}

} // namespace

// Shared by the element around the stack and its rows.
struct SelectionController {
    SelectionModel model;
    std::function<bool()> enabled;
    Axis axis = Axis::vertical;
    bool claimed = false;
    std::vector<ItemKey> keys;                   // the ForEach's, in order, as last read
    std::unordered_map<ItemKey, Element*> rows;  // the rows built now
    State<std::optional<ItemKey>> cursor;        // the row keys act on: the last one clicked or moved to
    std::optional<ItemKey> anchor;               // where Shift ranges start
    State<bool> focused{false};                  // the stack has keyboard focus, shown (RowConfiguration)
    std::function<void(const ItemKey&)> reveal;  // scrolls a row into view (the stack's element)

    std::vector<ItemKey> selected() const { return untrack(model.get); }

    // The keys from `from` to `to` in ForEach order, or `to` alone if `from` is gone.
    std::vector<ItemKey> range(const ItemKey& from, const ItemKey& to) const {
        const auto start = std::ranges::find(keys, from);
        const auto end = std::ranges::find(keys, to);
        if (start == keys.end() || end == keys.end()) {
            return {to};
        }
        return start <= end ? std::vector<ItemKey>(start, end + 1) : std::vector<ItemKey>(end, start + 1);
    }

    void click(const ItemKey& key, KeyModifiers modifiers) {
        if (model.multiple && has(modifiers, KeyModifiers::shift)) {
            model.set(range(anchor.value_or(key), key));
        } else if (has(modifiers, primary_modifier())) {
            // Adds or removes it (a single selection: selects it, or clears it).
            std::vector<ItemKey> chosen = selected();
            if (const auto found = std::ranges::find(chosen, key); found != chosen.end()) {
                chosen.erase(found);
            } else if (model.multiple) {
                chosen.push_back(key);
            } else {
                chosen = {key};
            }
            model.set(std::move(chosen));
            anchor = key;
        } else {
            model.set({key});
            anchor = key;
        }
        cursor = key;
    }

    // The keyboard: to `key`, extending the range from the anchor or selecting it alone.
    void move_to(const ItemKey& key, bool extend) {
        if (extend && model.multiple) {
            model.set(range(anchor.value_or(cursor.peek().value_or(key)), key));
        } else {
            model.set({key});
            anchor = key;
        }
        cursor = key;
        focused = true;
        if (reveal) {
            reveal(key);
        }
    }

    // Where the keys go from: the cursor, else the first selected row.
    std::optional<std::size_t> current_index() const {
        if (const std::optional<ItemKey>& at = cursor.peek()) {
            if (const auto found = std::ranges::find(keys, *at); found != keys.end()) {
                return static_cast<std::size_t>(found - keys.begin());
            }
        }
        const std::vector<ItemKey> chosen = selected();
        for (std::size_t i = 0; i < keys.size(); ++i) {
            if (std::ranges::find(chosen, keys[i]) != chosen.end()) {
                return i;
            }
        }
        return std::nullopt;
    }
};

std::shared_ptr<SelectionController> claim_selection(const LowerContext& context, Axis axis) {
    if (!context.selection || context.selection->claimed) {
        return nullptr;
    }
    context.selection->claimed = true;
    context.selection->axis = axis;
    return context.selection;
}

namespace {

// One row: a click selects it (with Shift and Ctrl/Cmd for ranges and toggles), a double click
// activates it, and the pointer over it shows as hover.
class RowElement final : public Element, private EventHandler {
public:
    RowElement(std::unique_ptr<Element> content, std::shared_ptr<SelectionController> selection, ItemKey key,
               State<bool> hovered, std::function<bool()> enabled)
        : content_(std::move(content)), selection_(std::move(selection)), key_(std::move(key)),
          hovered_(std::move(hovered)), enabled_(std::move(enabled)) {
        adopt(*content_);
        selection_->rows[key_] = this;
    }
    ~RowElement() override {
        if (const auto found = selection_->rows.find(key_); found != selection_->rows.end() && found->second == this) {
            selection_->rows.erase(found);
        }
    }

    void inspect(Inspection& out) const override { out.name = "Row"; }
    void accessibility(AccessibilityInfo& out) const override {
        out.role = AccessRole::list_box_option;
        out.selected = untrack([&] { return selection_->model.contains(key_); });
        out.disabled = !usable();
        out.can_click = !out.disabled;
    }
    bool accessibility_action(const AccessibilityAction& action) override {
        if (action.kind != AccessibilityAction::Kind::click || !usable()) {
            return false;
        }
        selection_->click(key_, KeyModifiers::none);
        return true;
    }

    HoverDelay effective_hover_delay() const override { return {.enter = 0, .exit = 0}; }
    float layout_priority() const noexcept override { return content_->layout_priority(); }
    float z_index() const noexcept override { return content_->z_index(); }
    const void* find_layout_value(const std::type_info& key) const noexcept override {
        return content_->find_layout_value(key);
    }
    EventHandler* event_handler() noexcept override { return this; }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    // Anywhere in the row, gaps too.
    bool hit_impl(Point point, std::vector<Element*>& path) override {
        return content_->hit_test(point, path) || inside(frame(), point);
    }

private:
    bool usable() const { return untrack(enabled_) && untrack(selection_->enabled); }

    EventResult pointer(const PointerEvent& event, Rect) override {
        // Selects on the press, like the platforms' lists; controls inside took theirs already.
        if (event.kind != PointerEvent::Kind::down || event.button != MouseButton::left || !usable()) {
            return {};
        }
        selection_->click(key_, event.modifiers);
        if (event.click_count == 2 && event.modifiers == KeyModifiers::none && selection_->model.activate) {
            selection_->model.activate(key_);
        }
        return {.consumed = true, .redraw = true, .cancellable = true};
    }
    EventResult hover(bool inside) override {
        hovered_ = inside;
        return {.redraw = true};
    }

    std::unique_ptr<Element> content_;
    std::shared_ptr<SelectionController> selection_;
    ItemKey key_;
    State<bool> hovered_;
    std::function<bool()> enabled_;
};

class RowNode final : public ViewNode {
public:
    RowNode(View content, ItemKey key, std::shared_ptr<SelectionController> selection)
        : content_(std::move(content)), key_(std::move(key)), selection_(std::move(selection)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        const std::shared_ptr<SelectionController> selection = selection_;
        const ItemKey key = key_;
        const State<bool> hovered(false);
        // Computed, so a row's style updates only when its own state changes.
        const Computed<bool> selected([selection, key] { return selection->model.contains(key); });
        const Computed<bool> focused([selection, key] { return selection->focused() && selection->cursor() == key; });
        const EnvironmentValues environment = context.environment;
        const auto enabled = [environment] { return !environment.get<DisabledKey>(); };

        LowerContext inner = context;
        inner.environment.set<RowSelectedKey>(Prop<bool>([selected] { return selected(); }));
        const RowConfiguration configuration{
            .content = content_,
            .axis = selection->axis,
            .is_selected = [selected] { return selected(); },
            .is_hovered = [hovered] { return hovered(); },
            .is_focused = [focused] { return focused(); },
            .is_enabled = enabled,
        };
        const std::shared_ptr<const RowStyle> style = untrack([&] { return environment.get<RowStyleKey>(); });
        View body;
        {
            const EnvironmentScope scope(inner.environment);
            body = style->make_body(configuration);
        }
        return std::make_unique<RowElement>(detail::lower(body, inner), selection, key, hovered, enabled);
    }

private:
    View content_;
    ItemKey key_;
    std::shared_ptr<SelectionController> selection_;
};

// Around the stack: one focus stop for its rows, taking the keys that move the selection.
class SelectionElement final : public Element, private EventHandler {
public:
    SelectionElement(std::unique_ptr<Element> content, std::shared_ptr<SelectionController> selection,
                     FocusManager* focus, bool* needs_frame)
        : content_(std::move(content)), selection_(std::move(selection)), focus_(focus), needs_frame_(needs_frame) {
        adopt(*content_);
        selection_->reveal = [this](const ItemKey& key) { reveal_row(key); };
    }
    ~SelectionElement() override { selection_->reveal = nullptr; }

    void inspect(Inspection& out) const override { out.name = "Selection"; }
    void accessibility(AccessibilityInfo& out) const override {
        out.role = AccessRole::list_box;
        out.multiselectable = selection_->model.multiple;
        out.disabled = !untrack(selection_->enabled);
        out.can_focus = !out.disabled;
    }

    float layout_priority() const noexcept override { return content_->layout_priority(); }
    float z_index() const noexcept override { return content_->z_index(); }
    bool is_spacer() const noexcept override { return content_->is_spacer(); }
    const void* find_layout_value(const std::type_info& key) const noexcept override {
        return content_->find_layout_value(key);
    }
    EventHandler* event_handler() noexcept override { return this; }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }
    bool always_arranges() const override { return pending_.has_value(); }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override {
        content_->place({bounds.x, bounds.y}, proposal);
        // A row a lazy stack hadn't built: aimed at its estimated place until it's built, then at
        // the row.
        if (pending_) {
            if (const auto row = selection_->rows.find(*pending_); row != selection_->rows.end()) {
                if (Element* scroll = scroll_container(*row->second)) {
                    scroll->reveal(row->second->frame());
                }
                pending_.reset();
            } else if (--settle_ <= 0) {
                pending_.reset();
            } else if (needs_frame_) {
                *needs_frame_ = true;
            }
        }
    }
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override { return content_->hit_test(point, path); }

private:
    bool focusable() const override { return untrack(selection_->enabled); }
    EventResult focus(bool focused) override {
        // Like a focus ring: shown for Tab, not for clicks.
        selection_->focused = focused && focus_ && focus_->ring_visible();
        return {.redraw = true};
    }

    EventResult key(const KeyEvent& event) override {
        if (event.kind != KeyEvent::Kind::down || !untrack(selection_->enabled) ||
            (focus_ && focus_->focused() != this)) {
            return {}; // keys for a control in a row stay its own
        }
        SelectionController& selection = *selection_;
        const std::vector<ItemKey>& keys = selection.keys;
        const bool vertical = selection.axis == Axis::vertical;
        const bool shift = has(event.modifiers, KeyModifiers::shift);
        const bool primary = has(event.modifiers, primary_modifier());
        const bool plain = !primary && !has(event.modifiers, KeyModifiers::alt);

        const Key back = vertical ? Key::up : Key::left;
        const Key forward = vertical ? Key::down : Key::right;
        if (plain && (event.key == back || event.key == forward || event.key == Key::home || event.key == Key::end)) {
            if (keys.empty()) {
                return {};
            }
            const std::size_t last = keys.size() - 1;
            const std::optional<std::size_t> at = selection.current_index();
            std::size_t to = 0;
            if (event.key == Key::home) {
                to = 0;
            } else if (event.key == Key::end) {
                to = last;
            } else if (event.key == forward) {
                to = at ? std::min(*at + 1, last) : 0;
            } else {
                to = at ? (*at == 0 ? 0 : *at - 1) : last;
            }
            selection.move_to(keys[to], shift);
            return {.consumed = true, .redraw = true};
        }
        if (event.key == Key::a && primary && !shift && selection.model.multiple) {
            selection.model.set(keys);
            return {.consumed = true, .redraw = true};
        }
        if (event.key == Key::enter && event.modifiers == KeyModifiers::none && selection.model.activate) {
            if (const std::optional<ItemKey> at = selection.cursor.peek(); at && !event.repeat) {
                selection.model.activate(*at);
            }
            return {.consumed = true};
        }
        return {};
    }

    // Scrolls the row into view: a built one at once; one a lazy stack hasn't built to where it
    // would be, then (once built, arrange()) to the row.
    void reveal_row(const ItemKey& key) {
        if (const auto row = selection_->rows.find(key); row != selection_->rows.end()) {
            if (Element* scroll = scroll_container(*row->second)) {
                scroll->reveal(row->second->frame());
            }
            return;
        }
        Element* stack = nullptr;
        std::optional<float> offset;
        std::function<void(Element&)> walk = [&](Element& element) {
            if (offset) {
                return;
            }
            if ((offset = element.item_offset(key))) {
                stack = &element;
                return;
            }
            element.for_each_child(walk);
        };
        walk(*content_);
        if (!stack) {
            return;
        }
        if (Element* scroll = scroll_container(*stack)) {
            const Rect frame = stack->frame();
            scroll->reveal(selection_->axis == Axis::vertical ? Rect{frame.x, frame.y + *offset, frame.width, 1}
                                                             : Rect{frame.x + *offset, frame.y, 1, frame.height});
        }
        pending_ = key;
        settle_ = 8;
        invalidate_paint_bounds(); // arranged again next frame
        if (needs_frame_) {
            *needs_frame_ = true;
        }
    }

    std::unique_ptr<Element> content_;
    std::shared_ptr<SelectionController> selection_;
    FocusManager* focus_;
    bool* needs_frame_;
    std::optional<ItemKey> pending_; // a row to reveal once it's built
    int settle_ = 0;                 // layouts left to wait for it
};

class SelectionNode final : public ViewNode {
public:
    SelectionNode(View content, SelectionModel model) : content_(std::move(content)), model_(std::move(model)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        auto selection = std::make_shared<SelectionController>();
        selection->model = model_;
        const EnvironmentValues environment = context.environment;
        selection->enabled = [environment] { return !environment.get<DisabledKey>(); };
        LowerContext inner = context;
        inner.selection = selection;
        auto content = detail::lower(content_, inner);
        if (!selection->claimed) {
            log_warning("selection(): no stack over a ForEach inside; put it on the VStack, HStack, LazyVStack or "
                        "LazyHStack whose rows it selects");
        }
        auto element = std::make_unique<SelectionElement>(std::move(content), selection, context.focus,
                                                          context.needs_frame);
        if (context.focus) {
            element->set_focus_manager(context.focus, {}); // the rows show focus, not a ring
        }
        return element;
    }

private:
    View content_;
    SelectionModel model_;
};

} // namespace

std::function<std::vector<ForEachItem>()> selectable_items(std::function<std::vector<ForEachItem>()> items,
                                                           std::shared_ptr<SelectionController> selection) {
    return [items = std::move(items), selection = std::move(selection)] {
        std::vector<ForEachItem> result = items();
        selection->keys.clear();
        selection->keys.reserve(result.size());
        for (ForEachItem& item : result) {
            selection->keys.push_back(item.key);
            item.view = [view = std::move(item.view), key = item.key, selection] {
                return ViewAccess::make(std::make_shared<const RowNode>(view(), key, selection));
            };
        }
        return result;
    };
}

} // namespace elg::detail

namespace elg {

View View::selection_impl(detail::SelectionModel model) const {
    return detail::ViewAccess::make(std::make_shared<const detail::SelectionNode>(*this, std::move(model)));
}

} // namespace elg
