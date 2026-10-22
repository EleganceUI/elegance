// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Styling: a custom theme, light and dark mode, a ButtonStyle, a reusable ViewModifier, and a
// custom environment value read by a component.

#include <elg/elegence.h>

#include <string>
#include <string_view>

using namespace elg;

// --- A button style: controls keep their behavior, the style decides how they look ---

struct PillButtonStyle : ButtonStyle {
    View make_body(const ButtonConfiguration& button) const override {
        const auto tint = Environment<TintKey>();
        return button.label
            .font(Font::headline)
            .foreground_style(Color::white)
            .padding(EdgeInsets::symmetric(18, 8))
            .background([=] { return button.is_pressed() ? tint().mix(Color::black, 0.2f) : tint(); }, Capsule())
            .opacity([=] { return button.is_enabled() ? 1.f : 0.5f; });
    }
};

// --- A view modifier: a reusable bundle of modifiers, applied with .modifier(Card{}) ---

struct Card : ViewModifier {
    View body(View content) const override {
        return content.padding(20)
            .frame({.max_width = infinity, .alignment = Alignment::leading})
            .background(ThemeColor::surface, RoundedRectangle(16))
            .shadow({.color = Color::black.opacity(0.12f), .radius = 12, .y = 4});
    }
};

// --- A custom environment value, set high up and read by any view below ---

struct CompactKey {
    using Value = bool;
    static constexpr std::string_view name = "styling.compact";
    static bool default_value(const EnvironmentValues&) { return false; }
};

// A component: a struct with `View body() const`. Its body runs where it's placed, so it can
// read the environment there.
struct SettingRow {
    std::string title;
    std::string detail;

    View body() const {
        const auto compact = Environment<CompactKey>();
        return HStack(Text(title),
                      Spacer(),
                      Text(detail).foreground_style(ThemeColor::secondary))
            .font([=] { return compact() ? Font::footnote : Font::body; });
    }
};

Theme purple_theme() {
    Theme theme = Theme::standard();
    theme.light.accent = Color::hex(0x7C3AED);
    theme.dark.accent = Color::hex(0xA78BFA);
    theme.dark.background = Color::hex(0x17141F);
    theme.dark.surface = Color::hex(0x221D2E);
    return theme;
}

View Settings() {
    const State<bool> dark(false);
    const State<bool> compact(false);

    return VStack({.spacing = 16},
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                         Text("Appearance").font(Font::headline),
                         Toggle("Dark mode", dark),
                         Toggle("Compact rows", compact))
                      .modifier(Card{}),
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                         Text("Account").font(Font::headline),
                         SettingRow{"Plan", "Pro"},
                         SettingRow{"Storage", "12.4 GB of 50 GB"},
                         SettingRow{"Renews", "March 3"})
                      .modifier(Card{}),
                  HStack(Button("Upgrade", [] {}), Button("Cancel plan", [] {}).disabled())
                      .button_style(PillButtonStyle{}))
        .environment<CompactKey>(compact)
        .padding(24)
        .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::top})
        .background(ThemeColor::background)
        .theme(purple_theme())
        .color_scheme([=] { return dark() ? ColorScheme::dark : ColorScheme::light; });
}

int main() {
    App app;

    app.window({.title = "Styling", .width = 480, .height = 520}, Settings);
    return app.run();
}
