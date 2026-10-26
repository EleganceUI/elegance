// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "text/font_library.h"

#include "core/log.h"
#include "platform/platform.h"

#include <elg/error.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_COLOR_H
#include FT_MULTIPLE_MASTERS_H
#include <hb.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <optional>

namespace elg {

namespace {

struct RegisteredFont {
    int weight;
    std::filesystem::path file;                              // a file, or
    std::shared_ptr<const std::vector<unsigned char>> data;  // bytes registered from memory
    std::string key;                                         // identifies the face once loaded
    bool icon = false;                                       // an icon set's font
};

std::mutex g_registry_mutex;
std::set<std::string, std::less<>> g_icon_families;
std::map<std::string, std::vector<RegisteredFont>, std::less<>> g_registry;
std::vector<std::filesystem::path> g_emoji_fonts;

} // namespace

void register_emoji_font(std::filesystem::path file) {
    if (!std::filesystem::is_regular_file(file)) {
        throw ResourceError(std::format("register_emoji_font(): \"{}\" is not a file", file.string()));
    }
    std::lock_guard lock(g_registry_mutex);
    g_emoji_fonts.push_back(std::move(file));
}

void register_font(std::string family, std::filesystem::path file, Weight weight) {
    if (family.empty()) {
        throw UsageError("register_font(): the family name is empty");
    }
    if (!std::filesystem::is_regular_file(file)) {
        throw ResourceError(std::format("register_font(): \"{}\" is not a file", file.string()));
    }
    std::lock_guard lock(g_registry_mutex);
    std::string key = file.string();
    g_registry[std::move(family)].push_back({static_cast<int>(weight), std::move(file), nullptr, std::move(key)});
}

void register_font(std::string family, std::span<const unsigned char> data, Weight weight) {
    if (family.empty()) {
        throw UsageError("register_font(): the family name is empty");
    }
    if (data.empty()) {
        throw ResourceError(std::format("register_font(): no font data for \"{}\"", family));
    }
    std::lock_guard lock(g_registry_mutex);
    auto bytes = std::make_shared<const std::vector<unsigned char>>(data.begin(), data.end());
    std::string key = std::format("memory:{}:{}", family, static_cast<int>(weight));
    g_registry[std::move(family)].push_back({static_cast<int>(weight), {}, std::move(bytes), std::move(key)});
}

} // namespace elg

namespace elg::detail {

void mark_icon_font_family(std::string family) {
    std::lock_guard lock(g_registry_mutex);
    g_icon_families.insert(std::move(family));
}

namespace {

RegisteredFont registered_font(const Font& font) {
    std::lock_guard lock(g_registry_mutex);
    const auto it = g_registry.find(font.family());
    if (it == g_registry.end() || it->second.empty()) {
        throw ResourceError(std::format("font family \"{}\" isn't registered; call elg::register_font() first",
                                        font.family()));
    }
    const int wanted = static_cast<int>(font.weight());
    const RegisteredFont* best = &it->second.front();
    for (const RegisteredFont& candidate : it->second) {
        if (std::abs(candidate.weight - wanted) < std::abs(best->weight - wanted)) {
            best = &candidate;
        }
    }
    RegisteredFont result = *best;
    result.icon = g_icon_families.contains(font.family());
    return result;
}

// Font files to try for the system UI font, best match first. (macOS's SF is a variable font:
// one file draws every weight.)
// TODO: real system font discovery (DirectWrite / CoreText / fontconfig).
std::vector<std::filesystem::path> system_font_candidates(Weight weight, [[maybe_unused]] bool italic) {
    const int w = static_cast<int>(weight);
    std::vector<std::filesystem::path> files;
#if defined(_WIN32)
    const std::filesystem::path dir =
        std::filesystem::path(platform::environment_variable("WINDIR").value_or("C:\\Windows")) / "Fonts";
    if (italic) {
        // Segoe UI's italics; upright ones follow if they're missing.
        if (w <= 300) {
            files.push_back(dir / "seguili.ttf");
        } else if (w >= 800) {
            files.push_back(dir / "seguibli.ttf");
        } else if (w >= 700) {
            files.push_back(dir / "segoeuiz.ttf");
        } else if (w >= 600) {
            files.push_back(dir / "seguisbi.ttf");
        }
        files.push_back(dir / "segoeuii.ttf");
    }
    if (w <= 300) {
        files.push_back(dir / "segoeuil.ttf");
    } else if (w >= 800) {
        files.push_back(dir / "seguibl.ttf");
    } else if (w >= 700) {
        files.push_back(dir / "segoeuib.ttf");
    } else if (w >= 600) {
        files.push_back(dir / "seguisb.ttf");
    }
    files.push_back(dir / "segoeui.ttf");
    files.push_back(dir / "arial.ttf");
#elif defined(__APPLE__)
    (void)w;
    (void)italic;
    files.push_back("/System/Library/Fonts/SFNS.ttf");
    files.push_back("/System/Library/Fonts/Helvetica.ttc");
#else
    if (w >= 600) {
        files.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");
        files.push_back("/usr/share/fonts/TTF/DejaVuSans-Bold.ttf");
        files.push_back("/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf");
    }
    files.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    files.push_back("/usr/share/fonts/TTF/DejaVuSans.ttf");
    files.push_back("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf");
    files.push_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
#endif
    return files;
}

RegisteredFont find_font(const Font& font) {
    if (!font.family().empty()) {
        return registered_font(font);
    }
    for (const std::filesystem::path& file : system_font_candidates(font.weight(), font.is_italic())) {
        if (std::filesystem::is_regular_file(file)) {
            return {static_cast<int>(font.weight()), file, nullptr, file.string()};
        }
    }
    throw ResourceError("no system font was found; register one with elg::register_font() and use "
                        "Font::custom()");
}

std::vector<unsigned char> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw ResourceError(std::format("can't open font file \"{}\"", path.string()));
    }
    // One bulk read: CJK fonts are tens of MB.
    const std::streamsize size = file.tellg();
    std::vector<unsigned char> data(static_cast<std::size_t>(std::max<std::streamsize>(size, 0)));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw ResourceError(std::format("can't read font file \"{}\"", path.string()));
    }
    return data;
}

// Fonts to fall back to for characters the requested font lacks, in order: symbols, emoji
// (monochrome until color glyphs, design.md §11), CJK, then other scripts.
std::vector<std::filesystem::path> fallback_font_candidates() {
    std::vector<std::filesystem::path> files;
#if defined(_WIN32)
    const std::filesystem::path dir =
        std::filesystem::path(platform::environment_variable("WINDIR").value_or("C:\\Windows")) / "Fonts";
    for (const char* name : {"seguisym.ttf", "seguiemj.ttf", "msyh.ttc", "YuGothR.ttc", "malgun.ttf", "Nirmala.ttf",
                             "ebrima.ttf", "gadugi.ttf", "seguihis.ttf", "simsun.ttc", "arial.ttf"}) {
        files.push_back(dir / name);
    }
#elif defined(__APPLE__)
    for (const char* file : {"/System/Library/Fonts/Apple Symbols.ttf", "/System/Library/Fonts/PingFang.ttc",
                             "/System/Library/Fonts/Hiragino Sans GB.ttc", "/System/Library/Fonts/AppleSDGothicNeo.ttc",
                             "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"}) {
        files.push_back(file);
    }
#else
    for (const char* file : {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                             "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
                             "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                             "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                             "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"}) {
        files.push_back(file);
    }
#endif
    return files;
}

} // namespace

// --- FontFace ---

FontFace::FontFace(FT_Library library, std::shared_ptr<const std::vector<unsigned char>> data,
                   const std::string& name, std::uint32_t id, bool icon, int weight)
    : id_(id), icon_(icon), data_(std::move(data)) {
    if (FT_New_Memory_Face(library, data_->data(), static_cast<FT_Long>(data_->size()), 0, &ft_face_) != 0) {
        throw ResourceError(std::format("\"{}\" isn't a supported font file", name));
    }
    units_per_em_ = ft_face_->units_per_EM > 0 ? static_cast<float>(ft_face_->units_per_EM) : 1000.f;

    hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(data_->data()),
                                     static_cast<unsigned>(data_->size()), HB_MEMORY_MODE_READONLY, nullptr, nullptr);
    hb_face_t* face = hb_face_create(blob, 0);
    hb_font_ = hb_font_create(face);
    hb_face_destroy(face);
    hb_blob_destroy(blob);

    // A variable font: its weight axis set for FreeType (outlines) and HarfBuzz (advances, metrics)
    // alike; the other axes keep their defaults.
    FT_MM_Var* axes = nullptr;
    if (FT_HAS_MULTIPLE_MASTERS(ft_face_) && FT_Get_MM_Var(ft_face_, &axes) == 0) {
        std::vector<FT_Fixed> coordinates(axes->num_axis);
        for (FT_UInt i = 0; i < axes->num_axis; ++i) {
            const FT_Var_Axis& axis = axes->axis[i];
            coordinates[i] = axis.def;
            if (axis.tag == FT_MAKE_TAG('w', 'g', 'h', 't')) {
                has_weight_axis_ = true;
                if (weight > 0) {
                    coordinates[i] = std::clamp(static_cast<FT_Fixed>(weight) * 65536, axis.minimum, axis.maximum);
                    variation_weight_ = static_cast<int>(coordinates[i] / 65536);
                }
            }
        }
        if (variation_weight_ > 0) {
            FT_Set_Var_Design_Coordinates(ft_face_, axes->num_axis, coordinates.data());
            const hb_variation_t variation{HB_TAG('w', 'g', 'h', 't'), static_cast<float>(variation_weight_)};
            hb_font_set_variations(hb_font_, &variation, 1);
        }
        FT_Done_MM_Var(library, axes);
    }
    // Positions come back in font units.
    hb_font_set_scale(hb_font_, static_cast<int>(units_per_em_), static_cast<int>(units_per_em_));

    hb_font_extents_t extents{};
    if (hb_font_get_h_extents(hb_font_, &extents)) {
        ascender_ = static_cast<float>(extents.ascender) / units_per_em_;
        descender_ = static_cast<float>(extents.descender) / units_per_em_;
        line_gap_ = static_cast<float>(extents.line_gap) / units_per_em_;
    } else {
        ascender_ = static_cast<float>(ft_face_->ascender) / units_per_em_;
        descender_ = static_cast<float>(ft_face_->descender) / units_per_em_;
    }
    // Color glyphs: the default palette, if the font has COLR/CPAL tables.
    if (FT_HAS_COLOR(ft_face_)) {
        FT_Palette_Data palettes{};
        FT_Color* colors = nullptr;
        if (FT_Palette_Data_Get(ft_face_, &palettes) == 0 && palettes.num_palette_entries > 0 &&
            FT_Palette_Select(ft_face_, 0, &colors) == 0 && colors) {
            for (FT_UShort i = 0; i < palettes.num_palette_entries; ++i) {
                const FT_Color& c = colors[i];
                palette_.push_back(Color{c.red / 255.f, c.green / 255.f, c.blue / 255.f, c.alpha / 255.f});
            }
        }
    }
    log_debug("loaded font {} ({}{})", name, id, palette_.empty() ? "" : ", color");
}

std::span<const ColorLayer> FontFace::color_layers(std::uint32_t glyph) const {
    if (palette_.empty()) {
        return {};
    }
    auto it = color_layers_.find(glyph);
    if (it == color_layers_.end()) {
        std::vector<ColorLayer> layers;
        FT_UInt layer_glyph = 0;
        FT_UInt color_index = 0;
        FT_LayerIterator iterator{};
        while (FT_Get_Color_Glyph_Layer(ft_face_, glyph, &layer_glyph, &color_index, &iterator)) {
            ColorLayer layer;
            layer.glyph = layer_glyph;
            if (color_index == 0xFFFF || color_index >= palette_.size()) {
                layer.foreground = true; // drawn in the text's color
            } else {
                layer.color = palette_[color_index];
            }
            layers.push_back(layer);
        }
        it = color_layers_.emplace(glyph, std::move(layers)).first;
    }
    return it->second;
}

bool FontFace::has_char(char32_t codepoint) const {
    return FT_Get_Char_Index(ft_face_, codepoint) != 0;
}

FontFace::~FontFace() {
    hb_font_destroy(hb_font_);
    FT_Done_Face(ft_face_);
}

// --- FontLibrary ---

FontLibrary::FontLibrary() {
    if (FT_Init_FreeType(&library_) != 0) {
        throw ResourceError("failed to initialize FreeType");
    }
}

FontLibrary::~FontLibrary() {
    faces_.clear(); // faces must go before the library
    FT_Done_FreeType(library_);
}

const FontFace& FontLibrary::load(const std::string& key, const std::filesystem::path& file) {
    auto it = faces_.find(key);
    if (it == faces_.end()) {
        auto data = std::make_shared<const std::vector<unsigned char>>(read_file(file));
        const auto id = static_cast<std::uint32_t>(faces_.size() + 1);
        it = faces_.emplace(key, std::make_unique<FontFace>(library_, std::move(data), key, id)).first;
    }
    return *it->second;
}

const FontFace* FontLibrary::fallback_for(char32_t codepoint, bool emoji) {
    auto& cache = emoji ? emoji_fallback_cache_ : fallback_cache_;
    if (const auto cached = cache.find(codepoint); cached != cache.end()) {
        return cached->second;
    }
    if (!fallback_files_listed_) {
        fallback_files_listed_ = true;
        fallback_files_ = fallback_font_candidates();
    }
    const auto fits = [&](const FontFace& face) { return face.has_char(codepoint) && (!emoji || face.has_color()); };
    const FontFace* found = nullptr;
    // The app's emoji fonts first (register_emoji_font), loaded as they're registered.
    if (emoji) {
        std::vector<std::filesystem::path> registered;
        {
            std::lock_guard lock(g_registry_mutex);
            registered.assign(g_emoji_fonts.begin() + static_cast<std::ptrdiff_t>(emoji_faces_.size() + emoji_fonts_failed_),
                              g_emoji_fonts.end());
        }
        for (const std::filesystem::path& file : registered) {
            try {
                emoji_faces_.push_back(&load(file.string(), file));
            } catch (const std::exception& failure) {
                ++emoji_fonts_failed_;
                log_warning("skipping emoji font {}: {}", file.string(), failure.what());
            }
        }
        for (const FontFace* face : emoji_faces_) {
            if (fits(*face)) {
                found = face;
                break;
            }
        }
    }
    for (const FontFace* face : fallbacks_) {
        if (found) {
            break;
        }
        if (fits(*face)) {
            found = face;
        }
    }
    // Not in the fonts loaded so far: load further down the chain until one has it.
    while (!found && fallbacks_tried_ < fallback_files_.size()) {
        const std::filesystem::path& file = fallback_files_[fallbacks_tried_++];
        std::error_code error;
        if (!std::filesystem::is_regular_file(file, error)) {
            continue;
        }
        try {
            const FontFace& face = load(file.string(), file);
            fallbacks_.push_back(&face);
            if (fits(face)) {
                found = &face;
            }
        } catch (const std::exception& failure) {
            log_warning("skipping fallback font {}: {}", file.string(), failure.what());
        }
    }
    if (!found && emoji) {
        found = fallback_for(codepoint); // no color font has it: any drawing will do
    }
    cache.emplace(codepoint, found);
    return found;
}

const FontFace& FontLibrary::resolve(const Font& font) {
    RegisteredFont source = find_font(font);
    auto it = faces_.find(source.key);
    if (it == faces_.end()) {
        auto data = source.data ? source.data
                                : std::make_shared<const std::vector<unsigned char>>(read_file(source.file));
        const auto id = static_cast<std::uint32_t>(faces_.size() + 1);
        it = faces_.emplace(source.key, std::make_unique<FontFace>(library_, std::move(data), source.key, id, source.icon))
                 .first;
    }
    const FontFace& file_face = *it->second;
    if (!file_face.has_weight_axis()) {
        return file_face; // one weight per file: the closest was picked
    }
    // A variable font: an instance at exactly the requested weight, sharing the file's bytes.
    const int weight = static_cast<int>(font.weight());
    const std::string key = std::format("{}@wght={}", source.key, weight);
    auto instance = faces_.find(key);
    if (instance == faces_.end()) {
        const auto id = static_cast<std::uint32_t>(faces_.size() + 1);
        instance = faces_.emplace(key, std::make_unique<FontFace>(library_, file_face.shared_bytes(), key, id,
                                                                  source.icon, weight))
                       .first;
    }
    return *instance->second;
}

} // namespace elg::detail
