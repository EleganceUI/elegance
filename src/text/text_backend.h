// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_TEXT_TEXT_BACKEND_H
#define ELG_TEXT_TEXT_BACKEND_H

// Text's backend interface (ELGSPEC-0001.R4): what text needs from the operating system,
// implemented in platform/<os>/fonts.cpp.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace elg::detail {

// Font files to try for the system UI font of `weight` (100 to 900, as elg::Weight) and
// `italic`, best match first. (macOS's SF is a variable font: one file draws every weight.)
std::vector<std::filesystem::path> system_font_candidates(int weight, bool italic);
// Fonts to fall back to for characters the requested font lacks, in order: symbols, emoji
// (monochrome until color glyphs, design.md §11), CJK, then other scripts.
std::vector<std::filesystem::path> fallback_font_candidates();

// The glyph cache's folder (text/glyph_cache.h), in the user's cache folder as the system names
// them, from the environment variables that `get` reads. nullopt when there's none.
std::optional<std::filesystem::path> glyph_cache_folder(
    const std::function<std::optional<std::string>(const char*)>& get);

} // namespace elg::detail

#endif // ELG_TEXT_TEXT_BACKEND_H
