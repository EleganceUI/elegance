// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Icon names and icon-set registration (design.md §13.2). Resolving a name needs no window; the
// fonts are only loaded when an Image is shown.

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <span>
#include <string>

using namespace elg;
using Catch::Matchers::ContainsSubstring;

namespace {

// Stand-in font bytes: registration only records them.
constexpr std::array<unsigned char, 4> kFakeFont{0, 1, 0, 0};

std::string error_of(auto&& make) {
    try {
        make();
    } catch (const UsageError& error) {
        return error.what();
    }
    return {};
}

} // namespace

TEST_CASE("built-in Phosphor icons resolve by name", "[image]") {
    CHECK_NOTHROW(Image({.system_name = "phosphor.heart"}));
    CHECK_NOTHROW(Image({.system_name = "phosphor.heart.fill"})); // a variant
    CHECK_NOTHROW(Image("heart"));                                // the default set
    CHECK_NOTHROW(Image("magnifying-glass"));
    CHECK_NOTHROW(Label("Search", {.system_name = "magnifying-glass"}));
}

TEST_CASE("Phosphor's extra weights and duotone resolve", "[image]") {
    CHECK_NOTHROW(Image({.system_name = "phosphor.heart.duotone"}));
    CHECK_NOTHROW(Image("gear").font_weight(Weight::thin));
    CHECK_NOTHROW(Image("gear").font_weight(Weight::light));
}

TEST_CASE("unknown icons name the closest matches", "[image]") {
    const std::string message = error_of([] { Image("phosphor.hart"); });
    CHECK_THAT(message, ContainsSubstring("no icon \"hart\""));
    CHECK_THAT(message, ContainsSubstring("\"phosphor.heart\""));
}

TEST_CASE("icon sets register from the maps they ship", "[image]") {
    SECTION("CSS, with the shared class prefix removed") {
        register_icon_set("test-css", {.font = std::span<const unsigned char>(kFakeFont),
                                       .names_data = R"(.ti.ti-star:before { content: "\e900"; }
                                                        .ti.ti-moon:before { content: "\e901"; })",
                                       .replace = true});
        CHECK_NOTHROW(Image("test-css.star"));
        CHECK_NOTHROW(Image("test-css.moon"));
        CHECK_THROWS_AS(Image("test-css.ti-star"), UsageError);
    }
    SECTION("JSON, hex strings or numbers") {
        register_icon_set("test-json",
                          {.font = std::span<const unsigned char>(kFakeFont), .names_data = R"({"bolt": "e902", "cloud": 59651})", .replace = true});
        CHECK_NOTHROW(Image("test-json.bolt"));
        CHECK_NOTHROW(Image("test-json.cloud"));
    }
    SECTION("Material-style codepoints") {
        register_icon_set("test-points",
                          {.font = std::span<const unsigned char>(kFakeFont), .names_data = "home e88a\nsearch e8b6\n", .replace = true});
        CHECK_NOTHROW(Image("test-points.home"));
    }
    SECTION("two-tone layers from :before / :after with opacity, in a variant with its own map") {
        register_icon_set("test-duo", {.font = std::span<const unsigned char>(kFakeFont),
                                       .variants = {{.name = "duo",
                                                     .font = std::span<const unsigned char>(kFakeFont),
                                                     .names_data = R"(.x-duo.x-cup:before { content: "\e910"; opacity: 0.2; }
                                                                      .x-duo.x-cup:after { content: "\e911"; margin-left: -1em; }
                                                                      .x-duo.x-mug:before { content: "\e912"; opacity: 0.2; }
                                                                      .x-duo.x-mug:after { content: "\e913"; margin-left: -1em; })"}},
                                       .names_data = "cup e900\nmug e901\n",
                                       .replace = true});
        CHECK_NOTHROW(Image("test-duo.cup"));
        CHECK_NOTHROW(Image("test-duo.cup.duo"));
    }
    SECTION("variants") {
        register_icon_set("test-variants", {.font = std::span<const unsigned char>(kFakeFont),
                                            .variants = {{.name = "fill", .font = std::span<const unsigned char>(kFakeFont)}},
                                            .names_data = "star e900\n",
                                            .replace = true});
        CHECK_NOTHROW(Image("test-variants.star.fill"));
    }
}

TEST_CASE("icon set registration errors", "[image]") {
    register_icon_set("test-once", {.font = std::span<const unsigned char>(kFakeFont), .names_data = "a e000\n", .replace = true});
    CHECK_THAT(error_of([] { register_icon_set("test-once", {.font = std::span<const unsigned char>(kFakeFont), .names_data = "a e000\n"}); }),
               ContainsSubstring("already registered"));
    CHECK_THROWS_AS(register_icon_set("has.dot", {.font = std::span<const unsigned char>(kFakeFont), .names_data = "a e000\n"}), UsageError);
    CHECK_THROWS_AS(register_icon_set("no-font", {.names_data = "a e000\n"}), UsageError);
    CHECK_THROWS_AS(register_icon_set("no-names", {.font = std::span<const unsigned char>(kFakeFont)}), UsageError);
}

TEST_CASE("the default set can change", "[image]") {
    register_icon_set("test-default", {.font = std::span<const unsigned char>(kFakeFont), .names_data = "only-here e000\n", .replace = true});
    CHECK_THROWS_AS(Image("only-here"), UsageError);
    set_default_icon_set("test-default");
    CHECK_NOTHROW(Image("only-here"));
    set_default_icon_set("phosphor");
}

TEST_CASE("raster image sources", "[image]") {
    // Building the view doesn't touch the file: loading happens when it's shown.
    CHECK_NOTHROW(Image({.file = "missing.png"}));
    CHECK_NOTHROW(Image({.file = "photo.jpg"}).resizable().aspect_ratio(ContentMode::fill).frame(100, 60));
    CHECK_NOTHROW(Image({.data = std::span<const unsigned char>(kFakeFont)}));
    CHECK_THAT(error_of([] { Image({.system_name = "heart", .file = "photo.jpg"}); }), ContainsSubstring("exactly one"));
    CHECK_THROWS_AS(Image(ImageSource{}), UsageError);
    // resizable() is for raster images, before other modifiers.
    CHECK_THROWS_AS(Image("heart").resizable(), UsageError);
    CHECK_THROWS_AS(Image({.file = "photo.jpg"}).padding(4).resizable(), UsageError);
    CHECK_THROWS_AS(Text("x").aspect_ratio(0.f), UsageError);
    CHECK_NOTHROW(Text("x").aspect_ratio(16.f / 9.f, ContentMode::fill));
}
