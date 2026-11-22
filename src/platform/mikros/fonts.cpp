// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Text's backend (text/text_backend.h) on Mikros: the fonts are those in /system/share/fonts, matched to the
// families that the configuration there names for sans-serif (platform/mikros/font_files.h).

#include "text/text_backend.h"

#include "platform/mikros/font_files.h"

namespace elg::detail {

std::vector<std::filesystem::path> system_font_candidates(int weight, bool italic) {
    const platform::mikros::FontCatalog& catalog = platform::mikros::system_font_catalog();
    return catalog.candidates(catalog.aliased("sans-serif"), weight, italic);
}

std::vector<std::filesystem::path> fallback_font_candidates() {
    return platform::mikros::system_font_catalog().fallbacks();
}

std::optional<std::filesystem::path> glyph_cache_folder(
    const std::function<std::optional<std::string>(const char*)>& get) {
    // XDG_CACHE_HOME is the cache folder, and HOME isn't used: without it there is no folder. Paths on Mikros
    // start with '/', whichever system builds this.
    if (const auto cache = get("XDG_CACHE_HOME"); cache && !cache->empty() && cache->front() == '/') {
        return std::filesystem::path(*cache) / "elegance" / "glyphs";
    }
    return std::nullopt;
}

} // namespace elg::detail
