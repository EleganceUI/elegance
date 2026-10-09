// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "text/font_library.h"

#include "core/log.h"
#include "platform/platform.h"

#include <elg/error.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>

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

} // namespace

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

// Font files to try for the system UI font, best match first.
// TODO: real system font discovery (DirectWrite / CoreText / fontconfig), variable font axes.
std::vector<std::filesystem::path> system_font_candidates(Weight weight) {
    const int w = static_cast<int>(weight);
    std::vector<std::filesystem::path> files;
#if defined(_WIN32)
    const std::filesystem::path dir =
        std::filesystem::path(platform::environment_variable("WINDIR").value_or("C:\\Windows")) / "Fonts";
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
    for (const std::filesystem::path& file : system_font_candidates(font.weight())) {
        if (std::filesystem::is_regular_file(file)) {
            return {static_cast<int>(font.weight()), file, nullptr, file.string()};
        }
    }
    throw ResourceError("no system font was found; register one with elg::register_font() and use "
                        "Font::custom()");
}

std::vector<unsigned char> read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw ResourceError(std::format("can't open font file \"{}\"", path.string()));
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

} // namespace

// --- FontFace ---

FontFace::FontFace(FT_Library library, std::shared_ptr<const std::vector<unsigned char>> data,
                   const std::string& name, std::uint32_t id, bool icon)
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
    log_debug("loaded font {} ({})", name, id);
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
    return *it->second;
}

} // namespace elg::detail
