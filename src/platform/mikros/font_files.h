// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_MIKROS_FONT_FILES_H
#define ELG_PLATFORM_MIKROS_FONT_FILES_H

// The system's fonts on Mikros, found without fontconfig's library: the default families that its
// configuration file names (parse_font_configuration).

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

} // namespace elg::detail::platform::mikros

#endif // ELG_PLATFORM_MIKROS_FONT_FILES_H
