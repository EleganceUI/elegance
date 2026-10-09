// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_ICONS_H
#define ELG_VIEW_ICONS_H

// Small UI glyphs for built-in controls, from the built-in icon set (Phosphor, design.md §13.2).
// Window caption buttons use Windows' own glyphs (Segoe MDL2 Assets) there, to match the system.
// Without icons (ELG_ICONS=OFF) they fall back to drawn shapes.

#include <elg/font.h>
#include <elg/view.h>

namespace elg::detail {

enum class Icon {
    minimize,
    maximize,
    restore,
    close,
    check,
    chevron_right,
    chevron_down,
};

// The icon at `size` points and `weight`, in the foreground style.
View icon(Icon which, float size = 10, Weight weight = Weight::regular);

} // namespace elg::detail

#endif // ELG_VIEW_ICONS_H
