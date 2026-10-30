// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Font files on Linux (text/font_library.h): DejaVu, Noto or Liberation, where
// distributions usually put them.
// TODO: real system font discovery (fontconfig).

#include "text/font_library.h"

#include <filesystem>
#include <vector>

namespace elg::detail {

std::vector<std::filesystem::path> system_font_candidates(Weight weight, bool) {
    const int w = static_cast<int>(weight);
    std::vector<std::filesystem::path> files;
    if (w >= 600) {
        files.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");
        files.push_back("/usr/share/fonts/TTF/DejaVuSans-Bold.ttf");
        files.push_back("/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf");
    }
    files.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    files.push_back("/usr/share/fonts/TTF/DejaVuSans.ttf");
    files.push_back("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf");
    files.push_back("/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf");
    return files;
}

std::vector<std::filesystem::path> fallback_font_candidates() {
    std::vector<std::filesystem::path> files;
    for (const char* file : {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
                             "/usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf",
                             "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                             "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
                             "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf"}) {
        files.push_back(file);
    }
    return files;
}

} // namespace elg::detail
