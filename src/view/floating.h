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
#include <optional>
#include <vector>

namespace elg::detail {

class Scope;
class TransitionElement;

// Where content of `size` goes by `anchor` (its top-leading corner), flipped to the other side
// of the anchor when it doesn't fit on its own and kept inside `area`. All in one coordinate space.
Point place_floating(Size size, Rect anchor, const FloatingPlacement& placement, Rect area);

// The environment a popup's content is being built in (for its style), or the default one.
EnvironmentValues styling_environment();

// Popup window content that comes and goes by `motion`: it animates in when built, and `exit`
// is set to its transition, to animate it out before the window closes.
View with_popup_motion(View content, const PopupMotion& motion, std::shared_ptr<TransitionElement*> exit);

// A token for one opening of floating content, unique in the process.
std::uint64_t next_floating_token() noexcept;

// Every window's root: its content, and above it the floating content that stays in the window
// (FloatingBoundary::window). It also closes the window's floating content on Esc (any boundary),
// and while content that closes on outside clicks is open, takes presses outside it.
// `.floating_container()` makes one too (`container`): it draws FloatingBoundary::container
// content inside it, clipped to it, and leaves Esc and outside clicks to the window's layer.
class FloatingLayerElement final : public Element, private EventHandler {
public:
    struct Panel {
        std::uint64_t token = 0;
        Element* anchor = nullptr;
        FloatingPlacement placement;
        FloatingDismiss dismiss;
        std::function<void()> dismissed; // closed by the user: Esc or an outside click
        bool interactive = true;         // false: the pointer passes through it (tooltips)
        std::optional<PopupMotion> motion; // how it comes and goes (element is then a transition)
        std::unique_ptr<Element> element; // null for content hosted elsewhere (a popup window)
        std::unique_ptr<Scope> scope;     // the content's effects; destroyed first
        // In the window's layer, for content a container's layer draws: what it closes is there.
        Element* hosted = nullptr;
    };

    explicit FloatingLayerElement(std::unique_ptr<Element> content, bool container = false);
    ~FloatingLayerElement() override;

    // The layer floating content with `boundary` goes in, for a view at `element`: the nearest
    // container's for FloatingBoundary::container (else the window's), the window's otherwise.
    static FloatingLayerElement* of(Element& element, FloatingBoundary boundary);

    // Shows `panel`, animating it in by its motion.
    void add(Panel panel);
    // Takes the panel away: with a motion, it animates out first (drawn, but not hit or seen by
    // the event dispatcher).
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
    bool always_arranges() const override { return !panels_.empty() || !exiting_.empty(); }

private:
    EventResult pointer(const PointerEvent& event, Rect frame) override;
    EventResult key(const KeyEvent& event) override;
    // The newest panel closing on `outside_click` (or Esc, with escape), if any.
    Panel* top(bool escape);
    void dismiss(Panel& panel);

    // Container layers: the window's layer above it, if it's in the tree.
    FloatingLayerElement* window_layer() const;

    std::unique_ptr<Element> content_;
    std::vector<Panel> panels_;  // oldest first
    std::vector<Panel> exiting_; // animating out
    bool container_ = false;
    bool closing_ = false;
};

} // namespace elg::detail

#endif // ELG_VIEW_FLOATING_H
