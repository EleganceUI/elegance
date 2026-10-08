// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_TEXT_TEXT_SYSTEM_H
#define ELG_TEXT_TEXT_SYSTEM_H

#include "text/font_library.h"
#include "text/glyph_atlas.h"

namespace elg::detail {

// Text state shared by every window of an app: loaded fonts and the glyph atlas. Lives on the
// UI thread for the duration of App::run(), outliving the element trees that use it.
struct TextSystem {
    FontLibrary fonts;
    GlyphAtlas atlas;
};

} // namespace elg::detail

#endif // ELG_TEXT_TEXT_SYSTEM_H
