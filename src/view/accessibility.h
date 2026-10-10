// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_ACCESSIBILITY_H
#define ELG_VIEW_ACCESSIBILITY_H

// The accessibility tree (design.md §12): built from the element tree when assistive technology
// asks for it, then handed to the platform adapter (AccessKit).

#include "view/element.h"

#include <elg/environment.h>
#include <elg/prop.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace elg::detail {

// What .accessibility_*(...) modifiers set; applied to the element they wrap (or the nearest one
// inside with a role).
struct AccessibilitySpec {
    std::optional<Prop<std::string>> label;
    std::optional<Prop<std::string>> value;
    std::optional<Prop<std::string>> hint;
    std::optional<AccessRole> role;
    std::optional<bool> hidden;
    bool combine = false;
    std::function<void()> action; // the default action (click), for custom controls
    // Built-in controls: state read when the tree is built.
    std::function<std::optional<bool>()> toggled;
    std::function<bool()> selected;
};

class AccessibilityElement final : public Element {
public:
    AccessibilityElement(AccessibilitySpec spec, EnvironmentValues environment, std::unique_ptr<Element> content)
        : spec_(std::move(spec)), environment_(std::move(environment)), content_(std::move(content)) {
        adopt(*content_);
    }

    const AccessibilitySpec& spec() const noexcept { return spec_; }
    const EnvironmentValues& environment() const noexcept { return environment_; }

    float layout_priority() const noexcept override { return content_->layout_priority(); }
    float z_index() const noexcept override { return content_->z_index(); }
    bool is_spacer() const noexcept override { return content_->is_spacer(); }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }
    const void* find_layout_value(const std::type_info& key) const noexcept override {
        return content_->find_layout_value(key);
    }
    void inspect(Inspection& out) const override { out.name = "Accessibility"; }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override { return content_->hit_test(point, path); }

private:
    AccessibilitySpec spec_;
    EnvironmentValues environment_;
    std::unique_ptr<Element> content_;
};

// Wraps an element (built-in controls use this for their role and state).
std::unique_ptr<Element> with_accessibility(std::unique_ptr<Element> element, AccessibilitySpec spec,
                                            EnvironmentValues environment = {});

struct AccessNode {
    std::uint64_t id = 0;
    AccessibilityInfo info;
    Rect bounds; // window points
    std::vector<std::uint64_t> children;
};

struct AccessTree {
    static constexpr std::uint64_t kRoot = 1;
    std::vector<AccessNode> nodes; // the root first
    std::uint64_t focus = kRoot;
    // What carries out actions on each node (valid until the next build).
    struct Target {
        Element* element = nullptr;
        std::function<void()> action; // a custom default action
    };
    std::unordered_map<std::uint64_t, Target> targets;
};

AccessTree build_accessibility_tree(Element& root, const std::string& window_title, Size window_size,
                                    const Element* focused);

} // namespace elg::detail

#endif // ELG_VIEW_ACCESSIBILITY_H
