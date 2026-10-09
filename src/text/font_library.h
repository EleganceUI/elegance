// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_TEXT_FONT_LIBRARY_H
#define ELG_TEXT_FONT_LIBRARY_H

#include <elg/font.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;
typedef struct hb_font_t hb_font_t;

namespace elg::detail {

// A loaded font file: FreeType for glyph outlines, HarfBuzz for shaping. Metrics are in em units.
class FontFace {
public:
    // `data` is the font file's bytes (shared: registered in-memory fonts aren't copied again);
    // `name` is for messages.
    FontFace(FT_Library library, std::shared_ptr<const std::vector<unsigned char>> data, const std::string& name,
             std::uint32_t id, bool icon = false);
    ~FontFace();

    FontFace(const FontFace&) = delete;
    FontFace& operator=(const FontFace&) = delete;

    std::uint32_t id() const noexcept { return id_; }
    // An icon set's font: glyphs get a single-channel distance field (see GlyphAtlas).
    bool icon() const noexcept { return icon_; }
    FT_Face ft_face() const noexcept { return ft_face_; }
    hb_font_t* hb_font() const noexcept { return hb_font_; }
    float units_per_em() const noexcept { return units_per_em_; }
    float ascender() const noexcept { return ascender_; }   // above the baseline, positive
    float descender() const noexcept { return descender_; } // below the baseline, negative
    float line_gap() const noexcept { return line_gap_; }

private:
    std::uint32_t id_;
    bool icon_;
    std::shared_ptr<const std::vector<unsigned char>> data_; // FreeType and HarfBuzz both read from this
    FT_Face ft_face_ = nullptr;
    hb_font_t* hb_font_ = nullptr;
    float units_per_em_ = 1000;
    float ascender_ = 0.8f;
    float descender_ = -0.2f;
    float line_gap_ = 0;
};

// Marks a register_font() family as an icon set's font (register_icon_set does this).
void mark_icon_font_family(std::string family);

// Resolves Font descriptions to loaded faces (system fonts and register_font() families).
// UI thread only.
class FontLibrary {
public:
    FontLibrary();
    ~FontLibrary();

    FontLibrary(const FontLibrary&) = delete;
    FontLibrary& operator=(const FontLibrary&) = delete;

    // Throws elg::ResourceError if the font can't be found or loaded.
    const FontFace& resolve(const Font& font);

private:
    FT_Library library_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<FontFace>> faces_; // by file path or memory key
};

} // namespace elg::detail

#endif // ELG_TEXT_FONT_LIBRARY_H
