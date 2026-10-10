// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Drop targets (design.md §13.4): View::on_drop.

#include "view/drop.h"

#include "view/view_node.h"

#include <elg/error.h>

namespace elg::detail {

namespace {

class DropNode final : public ViewNode {
public:
    DropNode(View content, DropOptions options) : content_(std::move(content)), options_(std::move(options)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        return std::make_unique<DropElement>(options_, detail::lower(content_, context));
    }

private:
    View content_;
    DropOptions options_;
};

bool inside(const Rect& r, Point p) {
    return p.x >= r.x && p.x < r.max_x() && p.y >= r.y && p.y < r.max_y();
}

DropElement* search(Element& element, Point point, bool files) {
    // Children first: the innermost target wins. Later children are drawn on top, so they're
    // asked first.
    std::vector<Element*> children;
    element.for_each_child([&](Element& child) { children.push_back(&child); });
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (DropElement* found = search(**it, point, files)) {
            return found;
        }
    }
    auto* target = dynamic_cast<DropElement*>(&element);
    return target && target->accepts(files) && inside(target->frame(), point) ? target : nullptr;
}

} // namespace

DropElement* find_drop_target(Element& root, Point point, bool files) {
    return search(root, point, files);
}

} // namespace elg::detail

namespace elg {

View View::on_drop(DropOptions options) const {
    if (!options.files && !options.text) {
        throw UsageError("on_drop: give it a .files or .text handler");
    }
    return detail::ViewAccess::make(std::make_shared<const detail::DropNode>(*this, std::move(options)));
}

} // namespace elg
