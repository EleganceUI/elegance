// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The glyph atlas: one page of distance-field glyphs, placed as frames draw them.

#include "text/glyph_atlas.h"
#include "text/text_system.h"

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>

using namespace elg;

TEST_CASE("a full glyph atlas starts over after the frame instead of dropping glyphs for good", "[text]") {
    detail::TextSystem text;
    const detail::FontFace& face = text.fonts.resolve(Font::system(12));
    // About twice what a page holds; the system fonts Elegance finds have more glyphs than this.
    constexpr std::uint32_t glyphs = 2500;

    // A glyph with an outline, found in an atlas of its own.
    std::optional<std::uint32_t> probe;
    {
        detail::GlyphAtlas scout;
        for (std::uint32_t id = 1; id < glyphs && !probe; ++id) {
            if (scout.find_or_add(face, id)) {
                probe = id;
            }
        }
    }
    REQUIRE(probe);

    // Every other glyph of the font, far more than a page holds: then there's no room for it.
    detail::GlyphAtlas atlas;
    for (std::uint32_t id = 1; id < glyphs; ++id) {
        if (id != *probe) {
            atlas.find_or_add(face, id);
        }
    }
    REQUIRE(atlas.find_or_add(face, *probe) == nullptr);

    // After the frame (its uploads taken), the atlas starts over and asks for a new frame.
    atlas.take_uploads();
    CHECK(atlas.has_uploads());
    CHECK(atlas.find_or_add(face, *probe) != nullptr);
}

