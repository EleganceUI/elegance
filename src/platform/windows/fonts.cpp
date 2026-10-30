// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Font files on Windows (text/font_library.h): Segoe UI, and Windows' own fonts
// for other scripts and symbols.
// TODO: real system font discovery (DirectWrite).

#include "text/font_library.h"

#include "platform/generic/platform.h"

#include <filesystem>
#include <vector>

namespace elg::detail {

std::vector<std::filesystem::path> system_font_candidates(Weight weight, bool italic) {
    const int w = static_cast<int>(weight);
    std::vector<std::filesystem::path> files;
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
    return files;
}

std::vector<std::filesystem::path> fallback_font_candidates() {
    std::vector<std::filesystem::path> files;
    const std::filesystem::path dir =
        std::filesystem::path(platform::environment_variable("WINDIR").value_or("C:\\Windows")) / "Fonts";
    for (const char* name : {"seguisym.ttf", "seguiemj.ttf", "msyh.ttc", "YuGothR.ttc", "malgun.ttf", "Nirmala.ttf",
                             "ebrima.ttf", "gadugi.ttf", "seguihis.ttf", "simsun.ttc", "arial.ttf"}) {
        files.push_back(dir / name);
    }
    return files;
}

} // namespace elg::detail
