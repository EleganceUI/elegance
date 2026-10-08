// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_FOCUS_H
#define ELG_VIEW_FOCUS_H

#include "view/element.h"

namespace elg::detail {

// One window's keyboard focus (design.md §10): which element keys go to first, and whether the
// focus ring shows. Like the web's :focus-visible, the ring shows for keyboard focus (Tab) and
// programmatic focus, not for clicks. Outlives the window's element tree.
class FocusManager {
public:
    Element* focused() const noexcept { return focused_; }
    bool ring_visible() const noexcept { return visible_; }

    // Moves focus to `element` (nullptr: none) and tells the handlers that lost and gained it.
    // Returns true when the window needs a new frame.
    bool focus(Element* element, bool visible);
    // Called when a focusable element is destroyed: drops focus without touching it.
    void forget(const Element* element) noexcept {
        if (focused_ == element) {
            focused_ = nullptr;
        }
    }

private:
    Element* focused_ = nullptr;
    bool visible_ = false;
};

} // namespace elg::detail

#endif // ELG_VIEW_FOCUS_H
