// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// WindowControls(): caption buttons for custom title bars, composed from the public API.

#include "view/view_node.h"

#include <elg/font.h>
#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/text.h>
#include <elg/views.h>
#include <elg/window.h>

#include <filesystem>

#if defined(_WIN32)
#include <windows.h> // the build defines WIN32_LEAN_AND_MEAN and NOMINMAX
#endif

namespace elg {

namespace {

constexpr std::string_view kIconFamily = "elg.caption-icons";

// Windows' own caption glyphs (Segoe MDL2 Assets), when the font is there.
bool caption_icons_available() {
    static const bool available = [] {
#if defined(_WIN32)
        wchar_t windows[MAX_PATH] = L"C:\\Windows";
        GetWindowsDirectoryW(windows, MAX_PATH);
        const std::filesystem::path file = std::filesystem::path(windows) / "Fonts" / "segmdl2.ttf";
        std::error_code error;
        if (std::filesystem::exists(file, error)) {
            register_font(std::string(kIconFamily), file);
            return true;
        }
#endif
        return false;
    }();
    return available;
}

enum class Caption { minimize, maximize, restore, close };

View caption_icon(Caption caption) {
    if (caption_icons_available()) {
        const char* glyph = caption == Caption::minimize   ? "\xEE\xA4\xA1"  // U+E921 ChromeMinimize
                            : caption == Caption::maximize ? "\xEE\xA4\xA2"  // U+E922 ChromeMaximize
                            : caption == Caption::restore  ? "\xEE\xA4\xA3"  // U+E923 ChromeRestore
                                                           : "\xEE\xA2\xBB"; // U+E8BB ChromeClose
        return Text(glyph).font(Font::custom(std::string(kIconFamily), 10));
    }
    // Drawn fallbacks.
    switch (caption) {
    case Caption::minimize: return Rectangle().frame(10, 1);
    case Caption::maximize: return Rectangle().stroke_border(ThemeColor::primary, 1).frame(10, 10);
    case Caption::restore:
        return ZStack({.alignment = Alignment::bottom_leading},
                      Rectangle().stroke_border(ThemeColor::primary, 1).frame(8, 8).offset(2, -2),
                      Rectangle().fill(ThemeColor::background).frame(8, 8),
                      Rectangle().stroke_border(ThemeColor::primary, 1).frame(8, 8))
            .frame(10, 10);
    case Caption::close: return Text("\xC3\x97").font(Font::system(16, Weight::light)); // U+00D7
    }
    return EmptyView();
}

// 46 x 32 caption buttons; close turns red, the others gray, while hovered.
class CaptionButtonStyle final : public ButtonStyle {
public:
    CaptionButtonStyle(bool close, WindowHandle window) : close_(close), window_(std::move(window)) {}

    View make_body(const ButtonConfiguration& button) const override {
        const auto theme = Environment<ThemeKey>();
        const auto scheme = Environment<ColorSchemeKey>();
        const auto colors = [=] { return theme().colors(scheme()); };
        const bool close = close_;
        const WindowHandle window = window_;
        return button.label
            .foreground_style([=] {
                const ColorTokens c = colors();
                if (close && (button.is_hovered() || button.is_pressed())) {
                    return Color::white;
                }
                return window.is_active() ? c.primary : c.tertiary;
            })
            .frame(46, 32)
            .background([=] {
                const ColorTokens c = colors();
                if (close) {
                    const Color red = Color::hex(0xC42B1C);
                    return button.is_pressed() ? red.opacity(0.8f) : button.is_hovered() ? red : Color::clear;
                }
                if (button.is_pressed()) {
                    return c.primary.opacity(0.14f);
                }
                return button.is_hovered() ? c.primary.opacity(0.08f) : Color::clear;
            });
    }

private:
    bool close_;
    WindowHandle window_;
};

// macOS-style: three 12 pt circles, gray while the window isn't active.
class TrafficLightStyle final : public ButtonStyle {
public:
    TrafficLightStyle(Color color, WindowHandle window) : color_(color), window_(std::move(window)) {}

    View make_body(const ButtonConfiguration& button) const override {
        const Color color = color_;
        const WindowHandle window = window_;
        return Circle()
            .fill([=](const EnvironmentValues& environment) {
                const Color base = window.is_active() ? color : environment.color(ThemeColor::outline);
                return button.is_pressed() ? base.mix(Color::black, 0.2f) : base;
            })
            .frame(12, 12);
    }

private:
    Color color_;
    WindowHandle window_;
};

View caption_controls(const WindowControlsOptions& options, const WindowHandle& window) {
    std::vector<View> buttons;
    if (options.minimize) {
        buttons.push_back(Button(caption_icon(Caption::minimize), [window] { window.minimize(); })
                              .button_style(CaptionButtonStyle(false, window)));
    }
    if (options.maximize) {
        buttons.push_back(Button(If([window] { return window.is_maximized(); }, caption_icon(Caption::restore),
                                    caption_icon(Caption::maximize)),
                                 [window] { window.toggle_maximized(); })
                              .button_style(CaptionButtonStyle(false, window)));
    }
    buttons.push_back(Button(caption_icon(Caption::close), [window] { window.close(); })
                          .button_style(CaptionButtonStyle(true, window)));
    return HStackLayout(VerticalAlignment::center, 0)(std::move(buttons));
}

View traffic_lights(const WindowControlsOptions& options, const WindowHandle& window) {
    std::vector<View> buttons;
    buttons.push_back(Button(EmptyView(), [window] { window.close(); })
                          .button_style(TrafficLightStyle(Color::hex(0xFF5F57), window)));
    if (options.minimize) {
        buttons.push_back(Button(EmptyView(), [window] { window.minimize(); })
                              .button_style(TrafficLightStyle(Color::hex(0xFEBC2E), window)));
    }
    if (options.maximize) {
        buttons.push_back(Button(EmptyView(), [window] { window.toggle_maximized(); })
                              .button_style(TrafficLightStyle(Color::hex(0x28C840), window)));
    }
    return HStackLayout(VerticalAlignment::center, 8)(std::move(buttons)).padding(Edges::horizontal, 12);
}

} // namespace

View WindowControls(WindowControlsOptions options) {
    // Built where it's used, so it acts on that window.
    return detail::ViewAccess::make(std::make_shared<const detail::DeferredNode>([options] {
        const WindowHandle window = Environment<WindowKey>()();
        WindowControlsStyle style = options.style;
        if (style == WindowControlsStyle::automatic) {
#if defined(__APPLE__)
            style = WindowControlsStyle::traffic_lights;
#else
            style = WindowControlsStyle::caption;
#endif
        }
        // Like native caption buttons, they never take keyboard focus.
        return (style == WindowControlsStyle::traffic_lights ? traffic_lights(options, window)
                                                             : caption_controls(options, window))
            .focusable(false);
    }));
}

} // namespace elg
