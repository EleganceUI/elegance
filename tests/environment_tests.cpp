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

// CompactKey as another module (a views module) would define it: another type, with its own copy
// of the name.
struct CompactKeyElsewhere {
    using Value = bool;
    static constexpr char text[] = {'t', 'e', 's', 't', '.', 'c', 'o', 'm', 'p', 'a', 'c', 't'};
    static constexpr std::string_view name{text, sizeof(text)};
    static bool default_value(const EnvironmentValues&) { return false; }
};

struct WideKey {
    using Value = bool;
    static constexpr std::string_view name = "test.wide";
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

TEST_CASE("keys are told apart by their names, not their types", "[hot-reload]") {
    EnvironmentValues environment;
    environment.set<CompactKey>(true);
    CHECK(environment.get<CompactKeyElsewhere>());
    CHECK_FALSE(environment.get<WideKey>());
    environment.set<CompactKeyElsewhere>(false);
    CHECK_FALSE(environment.get<CompactKey>());
    static_assert(detail::key_id(CompactKey::name) == detail::key_id(CompactKeyElsewhere::name));
    static_assert(detail::key_id(CompactKey::name) != detail::key_id(WideKey::name));
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

TEST_CASE("glass tints follow the color scheme, as the old material presets did") {
    EnvironmentValues environment;
    CHECK(same(environment.color(ThemeColor::glass_regular), Color{0.97f, 0.97f, 0.98f, 0.55f}));
    environment.set<ColorSchemeKey>(ColorScheme::dark);
    CHECK(same(environment.color(ThemeColor::glass_regular), Color{0.11f, 0.11f, 0.12f, 0.6f}));
    CHECK(same(environment.color(ThemeColor::glass_ultra_thick), Color{0.11f, 0.11f, 0.12f, 0.86f}));
}

TEST_CASE("a ThemeBlur prop resolves against the environment's theme") {
    const Prop<float> radius = ThemeBlur::xl;
    CHECK(radius.needs_environment());

    EnvironmentValues environment;
    CHECK(radius.get(environment) == 24);
    Theme theme = Theme::standard();
    theme.blur.xl = 30;
    environment.set<ThemeKey>(theme);
    CHECK(radius.get(environment) == 30);
}

TEST_CASE("an environment value set from State is reactive") {
    const Owner owner;
    const State<ColorScheme> scheme(ColorScheme::light);
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
