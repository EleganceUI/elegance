// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_MIKROS_FONT_FILES_H
#define ELG_PLATFORM_MIKROS_FONT_FILES_H

// The system's fonts on Mikros, found without fontconfig's library: the default families that its
// configuration file names (parse_font_configuration), the family, weight and slant that a font file
// says it has (read_font_face), and the files of a folder of fonts matched to families
// (FontCatalog). The folder is a parameter, for the tests.

#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace elg::detail::platform::mikros {

// The names fontconfig compares families by: ASCII lower case, without spaces.
std::string normalized_family(std::string_view family);

// The families that a fontconfig configuration names for each generic family, in the order it prefers
// them, by normalized generic name: from <alias> elements, the families under <prefer>, then
// <accept>, then <default>. Everything else in the file is ignored.
using FontAliases = std::map<std::string, std::vector<std::string>, std::less<>>;
FontAliases parse_font_configuration(std::string_view text);

// What a TrueType or OpenType font file says about its first font.
struct FontFace {
    std::filesystem::path file;
    std::string family;     // name ID 1
    std::string typographic; // name ID 16, or empty
    int weight = 400;       // 100 to 900
    int width = 5;          // 1 to 9, 5 is normal
    bool italic = false;
};

// nullopt for a file that isn't a font, or that is cut off or doesn't hold the tables it needs.
std::optional<FontFace> read_font_face(const std::filesystem::path& file);

// The fonts in a folder, and the fontconfig configuration in it (fonts.conf, if there is one).
class FontCatalog {
public:
    explicit FontCatalog(const std::filesystem::path& folder);

    // The families that the configuration names for `generic` ("sans-serif", "monospace"), or none.
    [[nodiscard]] std::vector<std::string> aliased(std::string_view generic) const;

    // Font files for text of `weight` (100 to 900) and `italic`, best first: the faces of each of
    // `families` in order, then every other face. A family is matched by its name ID 1 or its name
    // ID 16.
    [[nodiscard]] std::vector<std::filesystem::path> candidates(std::span<const std::string> families, int weight,
                                                                bool italic) const;

    // Fonts to fall back to for characters that the others lack: the regular face of the sans-serif
    // family, the one of the monospace family, then every other face by path.
    [[nodiscard]] std::vector<std::filesystem::path> fallbacks() const;

private:
    FontAliases aliases_;
    std::vector<FontFace> faces_;
};

// The catalog of the system's fonts folder, /system/share/fonts, made on first use.
const FontCatalog& system_font_catalog();

} // namespace elg::detail::platform::mikros

#endif // ELG_PLATFORM_MIKROS_FONT_FILES_H
