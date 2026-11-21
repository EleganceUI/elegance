// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The system's fonts as the Mikros build finds them (platform/mikros/font_files.h), on every system, in a
// folder of fonts that these tests make: fonts whose files say what family, weight and slant they have in
// their name tables, with names that don't (ELGSPEC-0040.R19). This is the test that includes the header of
// the Mikros platform, with a maintainer's permission.

#include "platform/mikros/font_files.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace elg::detail::platform::mikros;
namespace fs = std::filesystem;

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

void put16(std::vector<unsigned char>& out, std::size_t at, unsigned value) {
    out[at] = static_cast<unsigned char>(value >> 8);
    out[at + 1] = static_cast<unsigned char>(value & 0xFF);
}

void put32(std::vector<unsigned char>& out, std::size_t at, std::uint32_t value) {
    put16(out, at, value >> 16);
    put16(out, at + 2, value & 0xFFFF);
}

void append_utf16(std::vector<unsigned char>& out, const std::u16string& text) {
    for (const char16_t unit : text) {
        out.push_back(static_cast<unsigned char>(unit >> 8));
        out.push_back(static_cast<unsigned char>(unit & 0xFF));
    }
}

struct FontSpec {
    std::u16string family;
    std::u16string typographic;
    unsigned weight = 400;
    unsigned width = 5;
    bool italic = false;
};

// A TrueType file with the three tables that matter (head, name and OS/2), and no glyphs.
std::vector<unsigned char> make_font(const FontSpec& spec) {
    // name: a record for the family, and one for the typographic family when there is one.
    std::vector<unsigned char> name(6, 0);
    std::vector<unsigned char> strings;
    const auto record = [&](unsigned id, const std::u16string& text) {
        const std::size_t at = name.size();
        name.resize(at + 12);
        put16(name, at, 3);     // Windows
        put16(name, at + 2, 1); // Unicode BMP
        put16(name, at + 4, 0x409);
        put16(name, at + 6, id);
        put16(name, at + 8, static_cast<unsigned>(text.size() * 2));
        put16(name, at + 10, static_cast<unsigned>(strings.size()));
        append_utf16(strings, text);
    };
    record(1, spec.family);
    if (!spec.typographic.empty()) {
        record(16, spec.typographic);
    }
    const unsigned count = spec.typographic.empty() ? 1 : 2;
    put16(name, 2, count);
    put16(name, 4, static_cast<unsigned>(name.size()));
    name.insert(name.end(), strings.begin(), strings.end());

    std::vector<unsigned char> os2(64, 0);
    put16(os2, 4, spec.weight);
    put16(os2, 6, spec.width);
    put16(os2, 62, spec.italic ? 1 : 0);
    std::vector<unsigned char> head(54, 0);
    put16(head, 44, spec.italic ? 2 : 0);

    struct Table {
        const char* tag;
        const std::vector<unsigned char>* data;
    };
    const Table tables[] = {{"OS/2", &os2}, {"head", &head}, {"name", &name}};
    std::vector<unsigned char> file(12 + 16 * 3, 0);
    put32(file, 0, 0x00010000);
    put16(file, 4, 3);
    for (std::size_t i = 0; i < 3; ++i) {
        const std::size_t entry = 12 + i * 16;
        for (std::size_t c = 0; c < 4; ++c) {
            file[entry + c] = static_cast<unsigned char>(tables[i].tag[c]);
        }
        put32(file, entry + 8, static_cast<std::uint32_t>(file.size()));
        put32(file, entry + 12, static_cast<std::uint32_t>(tables[i].data->size()));
        file.insert(file.end(), tables[i].data->begin(), tables[i].data->end());
        while (file.size() % 4 != 0) {
            file.push_back(0);
        }
    }
    return file;
}

void write(const fs::path& file, const std::vector<unsigned char>& bytes) {
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void write(const fs::path& file, const std::string& text) {
    std::ofstream out(file, std::ios::binary);
    out << text;
}

// A folder that is removed, with what is in it, at the end of the test.
class Folder {
public:
    Folder() {
        // Test cases run in processes of their own, at the same time: the name has to differ between them.
        std::random_device random;
        do {
            path_ = fs::temp_directory_path() /
                    ("elegance-mikros-fonts-" + std::to_string(random()) + std::to_string(random()));
        } while (!fs::create_directories(path_));
    }
    ~Folder() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    Folder(const Folder&) = delete;
    Folder& operator=(const Folder&) = delete;
    Folder(Folder&&) = delete;
    Folder& operator=(Folder&&) = delete;
    [[nodiscard]] const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

std::vector<std::string> names(const std::vector<fs::path>& files) {
    std::vector<std::string> out;
    out.reserve(files.size());
    for (const fs::path& file : files) {
        out.push_back(file.filename().string());
    }
    return out;
}

using Strings = std::vector<std::string>;

// What `value` holds. The test fails when it holds nothing.
template <typename T>
T required(std::optional<T> value) {
    if (!value) {
        throw std::runtime_error("a value that the test needs is missing");
    }
    return std::move(*value);
}

// The fonts of the Mikros system, under file names that tell nothing: what the name tables say decides.
void make_fonts(const Folder& folder) {
    write(folder.path() / "a.ttf", make_font({.family = u"DejaVu Sans"}));
    write(folder.path() / "b.ttf", make_font({.family = u"DejaVu Sans", .weight = 700}));
    write(folder.path() / "c.ttf", make_font({.family = u"DejaVu Sans Mono"}));
    write(folder.path() / "d.ttf", make_font({.family = u"DejaVu Sans Mono", .weight = 700}));
    // A condensed face, whose typographic family is the sans family: it must not tie with the regular face.
    write(folder.path() / "0.ttf",
          make_font({.family = u"DejaVu Sans Condensed", .typographic = u"DejaVu Sans", .width = 3}));
    write(folder.path() / "f.ttf", make_font({.family = u"DejaVu Sans", .italic = true}));
    write(folder.path() / "fonts.conf", std::string(mikros_configuration));
    write(folder.path() / "LICENSE", std::string("Not a font."));
    std::vector<unsigned char> cut = make_font({.family = u"Cut"});
    cut.resize(40);
    write(folder.path() / "cut.ttf", cut);
}

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

TEST_CASE("on Mikros, a font file says its family, weight and slant in its name table", "[mikros]") {
    const Folder folder;
    make_fonts(folder);
    const FontFace regular = required(read_font_face(folder.path() / "a.ttf"));
    CHECK(regular.family == "DejaVu Sans");
    CHECK(regular.typographic.empty());
    CHECK(regular.weight == 400);
    CHECK(regular.width == 5);
    CHECK_FALSE(regular.italic);
    const FontFace bold = required(read_font_face(folder.path() / "b.ttf"));
    CHECK(bold.weight == 700);
    const FontFace condensed = required(read_font_face(folder.path() / "0.ttf"));
    CHECK(condensed.family == "DejaVu Sans Condensed");
    CHECK(condensed.typographic == "DejaVu Sans");
    CHECK(condensed.width == 3);
    const FontFace italic = required(read_font_face(folder.path() / "f.ttf"));
    CHECK(italic.italic);
}

TEST_CASE("on Mikros, names in a font's table are read as UTF-16, surrogate pairs included", "[mikros]") {
    const Folder folder;
    write(folder.path() / "x.ttf", make_font({.family = u"Café \U0001D11E"}));
    const FontFace face = required(read_font_face(folder.path() / "x.ttf"));
    CHECK(face.family == "Caf\xC3\xA9 \xF0\x9D\x84\x9E");
}

TEST_CASE("on Mikros, a file that isn't a whole font is not one", "[mikros]") {
    const Folder folder;
    make_fonts(folder);
    CHECK_FALSE(read_font_face(folder.path() / "LICENSE"));
    CHECK_FALSE(read_font_face(folder.path() / "cut.ttf"));
    CHECK_FALSE(read_font_face(folder.path() / "fonts.conf"));
    CHECK_FALSE(read_font_face(folder.path() / "does-not-exist.ttf"));
    CHECK_FALSE(read_font_face(folder.path()));
}

TEST_CASE("on Mikros, the fonts of the default families come first, best weight and slant first", "[mikros]") {
    const Folder folder;
    make_fonts(folder);
    const FontCatalog catalog(folder.path());
    CHECK(catalog.aliased("sans-serif") == Strings{"DejaVu Sans"});
    CHECK(catalog.aliased("Monospace") == Strings{"DejaVu Sans Mono"});
    CHECK(catalog.aliased("serif").empty());

    const Strings sans = catalog.aliased("sans-serif");
    // The condensed face has the sans family as its typographic family: it follows the regular face.
    CHECK(names(catalog.candidates(sans, 400, false)) == Strings{"a.ttf", "0.ttf", "b.ttf", "f.ttf", "c.ttf", "d.ttf"});
    CHECK(names(catalog.candidates(sans, 700, false)).front() == "b.ttf");
    CHECK(names(catalog.candidates(sans, 300, false)).front() == "a.ttf");
    CHECK(names(catalog.candidates(sans, 600, false)).front() == "b.ttf");
    // Only an upright and an italic regular face: a request for italic bold gets the italic one.
    CHECK(names(catalog.candidates(sans, 700, true)).front() == "f.ttf");

    const Strings mono = catalog.aliased("monospace");
    const Strings mono_files = names(catalog.candidates(mono, 400, false));
    CHECK(mono_files.front() == "c.ttf");
    CHECK(mono_files[1] == "d.ttf");
    CHECK(mono_files.size() == 6); // the rest follow, and what isn't a font is not among them
    CHECK(names(catalog.candidates(mono, 700, false)).front() == "d.ttf");

    // A family that no face has: every face, best first.
    const Strings other{"Nothing"};
    CHECK(names(catalog.candidates(other, 400, false)) == Strings{"a.ttf", "c.ttf", "0.ttf", "b.ttf", "d.ttf", "f.ttf"});
}

TEST_CASE("on Mikros, the fallback fonts are the regular sans and monospace faces, then the rest by path", "[mikros]") {
    const Folder folder;
    make_fonts(folder);
    const FontCatalog catalog(folder.path());
    CHECK(names(catalog.fallbacks()) == Strings{"a.ttf", "c.ttf", "0.ttf", "b.ttf", "d.ttf", "f.ttf"});
}

TEST_CASE("on Mikros, a folder without a configuration, or without fonts, still gives what it has", "[mikros]") {
    const Folder folder;
    write(folder.path() / "only.ttf", make_font({.family = u"Only"}));
    const FontCatalog catalog(folder.path());
    CHECK(catalog.aliased("sans-serif").empty());
    CHECK(names(catalog.candidates({}, 400, false)) == Strings{"only.ttf"});
    CHECK(names(catalog.fallbacks()) == Strings{"only.ttf"});

    const FontCatalog none(folder.path() / "absent");
    CHECK(none.candidates({}, 400, false).empty());
    CHECK(none.fallbacks().empty());
    CHECK(none.aliased("monospace").empty());
}

TEST_CASE("on Mikros, family names are compared without case or spaces", "[mikros]") {
    CHECK(normalized_family("DejaVu Sans Mono") == "dejavusansmono");
    CHECK(normalized_family("  Sans-Serif") == "sans-serif");
    const Folder folder;
    make_fonts(folder);
    const FontCatalog catalog(folder.path());
    const Strings spelled{"dejavusans mono"};
    CHECK(names(catalog.candidates(spelled, 400, false)).front() == "c.ttf");
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

TEST_CASE("on Mikros, the fallback fonts start with the faces of the default families that exist", "[mikros]") {
    const auto make = [](const Folder& folder, const std::string& sans) {
        write(folder.path() / "a.ttf", make_font({.family = u"Other", .weight = 700}));
        write(folder.path() / "z.ttf", make_font({.family = u"Zed"}));
        write(folder.path() / "fonts.conf",
              "<fontconfig><alias><family>sans-serif</family><prefer><family>" + sans +
                  "</family></prefer></alias></fontconfig>");
    };
    const Folder installed;
    make(installed, "Zed");
    CHECK(names(FontCatalog(installed.path()).fallbacks()) == Strings{"z.ttf", "a.ttf"});
    // A family that isn't installed doesn't make another face the regular sans: the faces follow by path.
    const Folder absent;
    make(absent, "Absent");
    CHECK(names(FontCatalog(absent.path()).fallbacks()) == Strings{"a.ttf", "z.ttf"});
}
