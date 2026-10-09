// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_TEXT_FONT_LIBRARY_H
#define ELG_TEXT_FONT_LIBRARY_H

#include <elg/color.h>
#include <elg/font.h>
#include <elg/geometry.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;
typedef struct hb_font_t hb_font_t;

namespace elg::detail {

// One layer of a color glyph (COLR): a plain glyph drawn in a palette color, or in the text's
// color for "foreground" layers.
struct ColorLayer {
    std::uint32_t glyph = 0;
    Color color;
    bool foreground = false;
};

// A COLRv1 fill: a solid color or a gradient, in em units from the glyph's origin, y down.
// "Foreground" colors take the text's color.
struct ColorStop {
    float offset = 0;
    Color color;
    bool foreground = false;
};
struct ColorPaint {
    enum class Kind { solid, linear, radial };
    Kind kind = Kind::solid;
    Color color; // solid
    bool foreground = false;
    std::vector<ColorStop> stops; // gradients
    Point p0, p1;                 // linear: start, end; radial: center
    float r0 = 0, r1 = 0;         // radial
};

// One filled region of a COLRv1 glyph: flattened, closed polygons in em units from the glyph's
// origin (y down), filled (non-zero) with `paint`.
struct ColorPath {
    std::shared_ptr<const std::vector<std::vector<Point>>> polygons;
    Rect bounds;
    ColorPaint paint;
};

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
    // Whether the font has color glyphs (COLR layers with a CPAL palette).
    bool has_color() const noexcept { return !palette_.empty(); }
    // A color glyph's layers, bottom to top; empty for ordinary glyphs. UI thread only.
    std::span<const ColorLayer> color_layers(std::uint32_t glyph) const;
    // A COLRv1 glyph's filled regions, bottom to top (gradients, transforms); empty for other
    // glyphs. Takes precedence over color_layers. UI thread only.
    std::span<const ColorPath> color_paths(std::uint32_t glyph) const;
    // Whether the font has a glyph for `codepoint`.
    bool has_char(char32_t codepoint) const;
    // The font file's bytes.
    const std::vector<unsigned char>& bytes() const noexcept { return *data_; }
    FT_Face ft_face() const noexcept { return ft_face_; }
    hb_font_t* hb_font() const noexcept { return hb_font_; }
    float units_per_em() const noexcept { return units_per_em_; }
    float ascender() const noexcept { return ascender_; }   // above the baseline, positive
    float descender() const noexcept { return descender_; } // below the baseline, negative
    float line_gap() const noexcept { return line_gap_; }

private:
    std::uint32_t id_;
    bool icon_;
    std::vector<Color> palette_; // the default CPAL palette, sRGB
    mutable std::unordered_map<std::uint32_t, std::vector<ColorLayer>> color_layers_;
    mutable std::unordered_map<std::uint32_t, std::vector<ColorPath>> color_paths_;
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

    // Font fallback (design.md §11): the first system fallback font that has `codepoint`, or
    // nullptr. The chain (symbols, emoji, CJK, other scripts) loads on first use. With `emoji`, a
    // color font that has it comes first (an emoji, not the symbol font's monochrome drawing).
    const FontFace* fallback_for(char32_t codepoint, bool emoji = false);

private:
    const FontFace& load(const std::string& key, const std::filesystem::path& file);

    FT_Library library_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<FontFace>> faces_; // by file path or memory key
    // The fallback chain: loaded one font at a time, only when the fonts before it don't have a
    // character (a CJK font isn't read until CJK text appears).
    std::vector<std::filesystem::path> fallback_files_;
    std::vector<const FontFace*> fallbacks_; // loaded so far, in chain order
    std::size_t fallbacks_tried_ = 0;        // files of the chain looked at so far
    bool fallback_files_listed_ = false;
    std::unordered_map<char32_t, const FontFace*> fallback_cache_;
    std::unordered_map<char32_t, const FontFace*> emoji_fallback_cache_;
};

} // namespace elg::detail

#endif // ELG_TEXT_FONT_LIBRARY_H
