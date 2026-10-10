// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The accessibility tree (design.md §12), and what the core elements are to assistive technology.

#include "view/accessibility.h"

#include "text/font_library.h"
#include "view/view_node.h"

#include <elg/error.h>

namespace elg::detail {

// --- The core elements ---

void TextElement::accessibility(AccessibilityInfo& out) const {
    if (face_ && face_->icon()) {
        out.hidden = true; // icons are decorative unless labeled
        return;
    }
    if (content_.empty()) {
        out.hidden = true; // nothing to read
        return;
    }
    out.role = AccessRole::label;
    out.label = content_;
}

void ButtonElement::accessibility(AccessibilityInfo& out) const {
    out.role = AccessRole::button;
    out.combine = true;
    out.disabled = enabled_ && !untrack(enabled_);
    out.can_click = !out.disabled;
    out.can_focus = !out.disabled;
}

bool ButtonElement::accessibility_action(const AccessibilityAction& action) {
    if (action.kind != AccessibilityAction::Kind::click || !untrack(enabled_) || !action_) {
        return false;
    }
    action_();
    return true;
}

void ScrollElement::accessibility(AccessibilityInfo& out) const {
    out.role = AccessRole::scroll_view;
}

bool ScrollElement::accessibility_action(const AccessibilityAction&) {
    return false; // scrolling children into view goes through reveal()
}

std::unique_ptr<Element> with_accessibility(std::unique_ptr<Element> element, AccessibilitySpec spec,
                                            EnvironmentValues environment) {
    return std::make_unique<AccessibilityElement>(std::move(spec), std::move(environment), std::move(element));
}

// --- Building the tree ---

namespace {

// Modifier settings on their way down to the element they describe.
struct Pending {
    std::optional<std::string> label;
    std::optional<std::string> value;
    std::optional<std::string> hint;
    std::optional<AccessRole> role;
    bool combine = false;
    std::function<void()> action;
    std::function<std::optional<bool>()> toggled;
    std::function<bool()> selected;

    bool any() const { return label || value || hint || role || combine || action || toggled || selected; }

    void absorb(const AccessibilityElement& element) {
        const AccessibilitySpec& spec = element.spec();
        const auto read = [&](const std::optional<Prop<std::string>>& prop, std::optional<std::string>& out) {
            if (prop && !out) { // the outermost modifier wins
                out = untrack([&] { return prop->get(element.environment()); });
            }
        };
        read(spec.label, label);
        read(spec.value, value);
        read(spec.hint, hint);
        if (spec.role && !role) {
            role = spec.role;
        }
        combine = combine || spec.combine;
        if (spec.action && !action) {
            action = spec.action;
        }
        if (spec.toggled && !toggled) {
            toggled = spec.toggled;
        }
        if (spec.selected && !selected) {
            selected = spec.selected;
        }
    }

    void apply(AccessibilityInfo& info) const {
        if (role) {
            info.role = *role;
        }
        if (label) {
            info.label = *label;
        }
        if (value) {
            info.value = *value;
        }
        if (hint) {
            info.hint = *hint;
        }
        info.combine = info.combine || combine;
        if (action) {
            info.can_click = true;
        }
        if (toggled) {
            info.toggled = untrack(toggled);
        }
        if (selected) {
            info.selected = untrack(selected);
        }
    }
};

class Builder {
public:
    explicit Builder(AccessTree& tree) : tree_(tree) {}

    void visit(Element& element, std::vector<std::uint64_t>& out, Pending pending) {
        if (const auto* wrapper = dynamic_cast<const AccessibilityElement*>(&element)) {
            if (wrapper->spec().hidden.value_or(false)) {
                return;
            }
            // Its settings apply to the element inside (a Toggle's role to its button); without one
            // with a role, the group made around its children below takes them.
            pending.absorb(*wrapper);
        }
        AccessibilityInfo info;
        element.accessibility(info);
        if (info.hidden) {
            return;
        }
        if (info.role != AccessRole::none) {
            add(element, info, out, std::move(pending));
            return;
        }
        // Transparent. Settings waiting for an element pass to an only child; around several
        // children they make a group.
        int count = 0;
        element.for_each_child([&](Element&) { ++count; });
        if (pending.any() && count != 1) {
            info.role = AccessRole::group;
            add(element, info, out, std::move(pending));
            return;
        }
        element.for_each_child([&](Element& child) { visit(child, out, count == 1 ? pending : Pending{}); });
    }

private:
    void add(Element& element, AccessibilityInfo info, std::vector<std::uint64_t>& out, Pending pending) {
        pending.apply(info);
        AccessNode node;
        node.id = element.accessibility_id();
        node.bounds = element.frame();
        if (info.combine) {
            if (info.label.empty()) {
                info.label = text_of(element);
            }
        } else {
            element.for_each_child([&](Element& child) { visit(child, node.children, {}); });
        }
        node.info = std::move(info);
        tree_.targets[node.id] = {&element, pending.action};
        out.push_back(node.id);
        tree_.nodes.push_back(std::move(node));
    }

    // The text inside an element, for a combined label: labels of what's inside, in order.
    static std::string text_of(Element& element) {
        std::string text;
        const std::function<void(Element&)> gather = [&](Element& e) {
            if (const auto* wrapper = dynamic_cast<const AccessibilityElement*>(&e)) {
                if (wrapper->spec().hidden.value_or(false)) {
                    return;
                }
                if (wrapper->spec().label) {
                    const std::string label =
                        untrack([&] { return wrapper->spec().label->get(wrapper->environment()); });
                    text += (text.empty() || label.empty() ? "" : " ") + label;
                    return;
                }
            }
            AccessibilityInfo info;
            e.accessibility(info);
            if (info.hidden) {
                return;
            }
            if (info.role == AccessRole::label && !info.label.empty()) {
                text += (text.empty() ? "" : " ") + info.label;
                return;
            }
            e.for_each_child(gather);
        };
        element.for_each_child(gather);
        return text;
    }

    AccessTree& tree_;
};

} // namespace

AccessTree build_accessibility_tree(Element& root, const std::string& window_title, Size window_size,
                                    const Element* focused) {
    AccessTree tree;
    tree.nodes.reserve(64);
    AccessNode window;
    window.id = AccessTree::kRoot;
    window.info.role = AccessRole::window;
    window.info.label = window_title;
    window.bounds = {0, 0, window_size.width, window_size.height};
    tree.nodes.push_back(window);
    std::vector<std::uint64_t> children;
    Builder(tree).visit(root, children, {});
    tree.nodes.front().children = std::move(children);
    // Focus: the focused element's node, or the nearest one around it.
    for (const Element* e = focused; e; e = e->parent()) {
        if (tree.targets.contains(e->accessibility_id())) {
            tree.focus = e->accessibility_id();
            break;
        }
    }
    return tree;
}

} // namespace elg::detail

// --- The .accessibility_*(...) modifiers ---

namespace elg::detail {

namespace {

AccessRole role_of(AccessibilityRole role) {
    switch (role) {
    case AccessibilityRole::group: return AccessRole::group;
    case AccessibilityRole::label: return AccessRole::label;
    case AccessibilityRole::heading: return AccessRole::heading;
    case AccessibilityRole::button: return AccessRole::button;
    case AccessibilityRole::link: return AccessRole::link;
    case AccessibilityRole::image: return AccessRole::image;
    case AccessibilityRole::toggle: return AccessRole::toggle;
    case AccessibilityRole::checkbox: return AccessRole::checkbox;
    case AccessibilityRole::slider: return AccessRole::slider;
    case AccessibilityRole::progress: return AccessRole::progress;
    case AccessibilityRole::text_field: return AccessRole::text_field;
    case AccessibilityRole::list: return AccessRole::list;
    case AccessibilityRole::list_item: return AccessRole::list_item;
    case AccessibilityRole::tab: return AccessRole::tab;
    case AccessibilityRole::menu_item: return AccessRole::menu_item;
    }
    return AccessRole::group;
}

class AccessibilityNode final : public ViewNode {
public:
    AccessibilityNode(View content, AccessibilitySpec spec) : content_(std::move(content)), spec_(std::move(spec)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        return with_accessibility(detail::lower(content_, context), spec_, context.environment);
    }

private:
    View content_;
    AccessibilitySpec spec_;
};

View accessible(const View& view, AccessibilitySpec spec) {
    return ViewAccess::make(std::make_shared<const AccessibilityNode>(view, std::move(spec)));
}

} // namespace

} // namespace elg::detail

namespace elg {

View View::accessibility_label(Prop<std::string> label) const {
    detail::AccessibilitySpec spec;
    spec.label = std::move(label);
    return detail::accessible(*this, std::move(spec));
}

View View::accessibility_value(Prop<std::string> value) const {
    detail::AccessibilitySpec spec;
    spec.value = std::move(value);
    return detail::accessible(*this, std::move(spec));
}

View View::accessibility_hint(Prop<std::string> hint) const {
    detail::AccessibilitySpec spec;
    spec.hint = std::move(hint);
    return detail::accessible(*this, std::move(spec));
}

View View::accessibility_hidden(bool hidden) const {
    detail::AccessibilitySpec spec;
    spec.hidden = hidden;
    return detail::accessible(*this, std::move(spec));
}

View View::accessibility_role(AccessibilityRole role) const {
    detail::AccessibilitySpec spec;
    spec.role = detail::role_of(role);
    return detail::accessible(*this, std::move(spec));
}

View View::accessibility_element() const {
    detail::AccessibilitySpec spec;
    spec.combine = true;
    return detail::accessible(*this, std::move(spec));
}

View View::accessibility_action(std::function<void()> action) const {
    if (!action) {
        throw UsageError("accessibility_action: the action is empty");
    }
    detail::AccessibilitySpec spec;
    spec.action = std::move(action);
    return detail::accessible(*this, std::move(spec));
}

} // namespace elg
