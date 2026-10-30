// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Font files on macOS (text/font_library.h): SF, and Apple's fonts for other scripts and symbols.
// TODO: real system font discovery (CoreText).

#include "text/font_library.h"

#include <filesystem>
#include <vector>

namespace elg::detail {

std::vector<std::filesystem::path> system_font_candidates(Weight, bool) {
    std::vector<std::filesystem::path> files;
    files.push_back("/System/Library/Fonts/SFNS.ttf");
    files.push_back("/System/Library/Fonts/Helvetica.ttc");
    return files;
}

std::vector<std::filesystem::path> fallback_font_candidates() {
    std::vector<std::filesystem::path> files;
    for (const char* file : {"/System/Library/Fonts/Apple Symbols.ttf", "/System/Library/Fonts/PingFang.ttc",
                             "/System/Library/Fonts/Hiragino Sans GB.ttc", "/System/Library/Fonts/AppleSDGothicNeo.ttc",
                             "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"}) {
        files.push_back(file);
    }
    return files;
}

} // namespace elg::detail
