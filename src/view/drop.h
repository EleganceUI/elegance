// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_DROP_H
#define ELG_VIEW_DROP_H

#include "view/element.h"

#include <elg/view.h>

namespace elg::detail {

// A view with .on_drop(...): layout, paint, and hit testing pass through to its content.
class DropElement final : public Element {
public:
    DropElement(DropOptions options, std::unique_ptr<Element> content)
        : options_(std::move(options)), content_(std::move(content)) {
        adopt(*content_);
    }

    const DropOptions& options() const noexcept { return options_; }
    bool accepts(bool files) const noexcept { return files ? bool(options_.files) : bool(options_.text); }

    float layout_priority() const noexcept override { return content_->layout_priority(); }
    float z_index() const noexcept override { return content_->z_index(); }
    bool is_spacer() const noexcept override { return content_->is_spacer(); }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }
    const void* find_layout_value(const std::type_info& key) const noexcept override {
        return content_->find_layout_value(key);
    }
    void inspect(Inspection& out) const override {
        out.name = "OnDrop";
        out.detail = options_.files && options_.text ? "files, text" : options_.files ? "files" : "text";
    }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override { return content_->hit_test(point, path); }

private:
    DropOptions options_;
    std::unique_ptr<Element> content_;
};

// The innermost drop target at `point` (window points) that takes files (or text).
DropElement* find_drop_target(Element& root, Point point, bool files);

} // namespace elg::detail

#endif // ELG_VIEW_DROP_H
