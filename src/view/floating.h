// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_FLOATING_H
#define ELG_VIEW_FLOATING_H

// Floating content (View::floating(), design.md §13.1): where it goes, and the layer that draws it
// inside a window. Content that may leave the window is a popup window the app opens
// (WindowServices::open_popup).

#include "view/element.h"

#include <elg/view.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace elg::detail {

class Scope;

// Where content of `size` goes by `anchor` (its top-leading corner), flipped to the other side
// of the anchor when it doesn't fit on its own and kept inside `area`. All in one coordinate space.
Point place_floating(Size size, Rect anchor, const FloatingPlacement& placement, Rect area);

// A token for one opening of floating content, unique in the process.
std::uint64_t next_floating_token() noexcept;

// Every window's root: its content, and above it the floating content that stays in the window
// (FloatingBoundary::window). It also closes the window's floating content on Esc (any boundary),
// and while content that closes on outside clicks is open, takes presses outside it.
class FloatingLayerElement final : public Element, private EventHandler {
public:
    struct Panel {
        std::uint64_t token = 0;
        Element* anchor = nullptr;
        FloatingPlacement placement;
        FloatingDismiss dismiss;
        std::function<void()> dismissed; // closed by the user: Esc or an outside click
        std::unique_ptr<Element> element; // null for content hosted elsewhere (a popup window)
        std::unique_ptr<Scope> scope;     // the content's effects; destroyed first
    };

    explicit FloatingLayerElement(std::unique_ptr<Element> content);
    ~FloatingLayerElement() override;

    // The layer `element` is in, if any.
    static FloatingLayerElement* of(Element& element);

    void add(Panel panel);
    void remove(std::uint64_t token);

    float layout_priority() const noexcept override { return content_->layout_priority(); }
    void for_each_child(const std::function<void(Element&)>& visit) override;
    EventHandler* event_handler() noexcept override { return this; }
    void inspect(Inspection& out) const override { out.name = "FloatingLayer"; }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override;
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>& path) override;
    // Anchors move without telling the layer.
    bool always_arranges() const override { return !panels_.empty(); }

private:
    EventResult pointer(const PointerEvent& event, Rect frame) override;
    EventResult key(const KeyEvent& event) override;
    // The newest panel closing on `outside_click` (or Esc, with escape), if any.
    Panel* top(bool escape);
    void dismiss(Panel& panel);

    std::unique_ptr<Element> content_;
    std::vector<Panel> panels_; // oldest first
    bool closing_ = false;
};

} // namespace elg::detail

#endif // ELG_VIEW_FLOATING_H
