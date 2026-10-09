// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_ICONS_H
#define ELG_VIEW_ICONS_H

// Small UI glyphs for built-in controls: Windows' own icon font (Segoe MDL2 Assets) when it's
// installed, drawn or text fallbacks elsewhere. (Until vector paths land, design.md §5.)

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

// The icon at `size` points, in the foreground style.
View icon(Icon which, float size = 10);

} // namespace elg::detail

#endif // ELG_VIEW_ICONS_H
