// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The system's fonts as the Mikros build finds them (platform/mikros/font_files.h), on every system: the
// default families that a fontconfig configuration names (ELGSPEC-0040.R19). This is the test that includes
// the header of the Mikros platform, with a maintainer's permission.

#include "platform/mikros/font_files.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace elg::detail::platform::mikros;

namespace {

// The fontconfig configuration that Mikros installs.
constexpr const char* mikros_configuration = R"(<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "urn:fontconfig:fonts.dtd">
<!-- The fontconfig configuration of the system fonts. -->
<fontconfig>
  <dir>/system/share/fonts</dir>
  <cachedir>/system/tmp/fontconfig</cachedir>
  <alias>
    <family>sans-serif</family>
    <prefer><family>DejaVu Sans</family></prefer>
  </alias>
  <alias>
    <family>monospace</family>
    <prefer><family>DejaVu Sans Mono</family></prefer>
  </alias>
</fontconfig>
)";

using Strings = std::vector<std::string>;

} // namespace

TEST_CASE("on Mikros, the configuration names the default families", "[mikros]") {
    const FontAliases aliases = parse_font_configuration(mikros_configuration);
    CHECK(aliases.size() == 2);
    CHECK(aliases.at("sans-serif") == Strings{"DejaVu Sans"});
    CHECK(aliases.at("monospace") == Strings{"DejaVu Sans Mono"});
}

TEST_CASE("on Mikros, the configuration is read for aliases only, in the order fontconfig prefers", "[mikros]") {
    const FontAliases aliases = parse_font_configuration(R"(
        <fontconfig>
          <!-- <alias><family>serif</family><prefer><family>Hidden</family></prefer></alias> -->
          <match><test name="family"><string>x</string></test><edit name="family"><string>y</string></edit></match>
          <alias binding="same">
            <family>Sans-Serif</family>
            <default><family>Last</family></default>
            <accept><family>Second</family></accept>
            <prefer><family>First &amp; Co</family><family> Next </family></prefer>
          </alias>
          <alias><family>sans-serif</family><prefer><family>Later alias</family></prefer></alias>
          <alias><family>mono space</family><prefer><family>&#68;ejaVu</family></prefer></alias>
        </fontconfig>)");
    CHECK(aliases.at("sans-serif") == Strings{"First & Co", "Next", "Second", "Last", "Later alias"});
    CHECK(aliases.at("monospace") == Strings{"DejaVu"});
    CHECK_FALSE(aliases.contains("serif"));
    CHECK(parse_font_configuration("").empty());
    CHECK(parse_font_configuration("<fontconfig><alias><family>a</fam").empty());
}

TEST_CASE("on Mikros, a reference that is no character stays as it was written", "[mikros]") {
    const FontAliases aliases = parse_font_configuration(
        "<fontconfig><alias><family>sans-serif</family><prefer>"
        "<family>a &#-1; b</family><family>c &#xD800; d</family><family>e &#x110000; f</family>"
        "<family>g &#65x; h</family><family>i &; j</family><family>k &#66; l</family>"
        "</prefer></alias></fontconfig>");
    CHECK(aliases.at("sans-serif") ==
          Strings{"a &#-1; b", "c &#xD800; d", "e &#x110000; f", "g &#65x; h", "i &; j", "k B l"});
}
