// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include <elg/environment.h>
#include <elg/error.h>
#include <elg/gradient.h>

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using namespace elg;

namespace {

struct CompactKey {
    using Value = bool;
    static constexpr std::string_view name = "test.compact";
    static bool default_value(const EnvironmentValues&) { return false; }
};

struct Owner {
    detail::Scope scope;
    detail::Scope::Use use{scope};
};

bool same(Color a, Color b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

} // namespace

TEST_CASE("keys fall back to their defaults and can be overridden") {
    EnvironmentValues environment;
    CHECK_FALSE(environment.get<CompactKey>());
    environment.set<CompactKey>(true);
    CHECK(environment.get<CompactKey>());
}

TEST_CASE("setting a key doesn't affect copies made earlier") {
    EnvironmentValues outer;
    const EnvironmentValues before = outer;
    outer.set<CompactKey>(true);
    CHECK_FALSE(before.get<CompactKey>());
    CHECK(outer.get<CompactKey>());
}

TEST_CASE("theme colors follow the color scheme") {
    EnvironmentValues environment;
    const Theme theme = Theme::standard();
    CHECK(same(environment.get<ForegroundKey>(), theme.light.primary));
    environment.set<ColorSchemeKey>(ColorScheme::dark);
    CHECK(same(environment.get<ForegroundKey>(), theme.dark.primary));
    CHECK(same(environment.get<TintKey>(), theme.dark.accent));
}

TEST_CASE("a ThemeColor prop resolves against the environment it's used in") {
    const Prop<Color> secondary = ThemeColor::secondary;
    CHECK(secondary.needs_environment());
    CHECK_THROWS_AS(secondary.get(), UsageError);

    EnvironmentValues environment;
    environment.set<ColorSchemeKey>(ColorScheme::dark);
    CHECK(same(secondary.get(environment), Theme::standard().dark.secondary));

    // Works for any type a Color converts to.
    const Prop<ShapeStyle> fill = ThemeColor::accent;
    CHECK(fill.needs_environment());
}

TEST_CASE("an environment value set from State is reactive") {
    Owner owner;
    State<ColorScheme> scheme(ColorScheme::light);
    EnvironmentValues environment;
    environment.set<ColorSchemeKey>(scheme);

    std::vector<Color> seen;
    effect([&] { seen.push_back(environment.get<ForegroundKey>()); });
    scheme = ColorScheme::dark;
    detail::flush_effects();

    REQUIRE(seen.size() == 2);
    CHECK(same(seen[0], Theme::standard().light.primary));
    CHECK(same(seen[1], Theme::standard().dark.primary));
}

TEST_CASE("a ThemeColor set as an environment value resolves where it was set") {
    EnvironmentValues environment;
    environment.set<ColorSchemeKey>(ColorScheme::dark);
    environment.set<ForegroundKey>(ThemeColor::secondary);
    environment.set<ColorSchemeKey>(ColorScheme::light); // inner override doesn't re-resolve it
    CHECK(same(environment.get<ForegroundKey>(), Theme::standard().dark.secondary));
}

TEST_CASE("Environment<K>() is only available while building views") {
    CHECK_THROWS_AS(Environment<FontKey>(), UsageError);
}
