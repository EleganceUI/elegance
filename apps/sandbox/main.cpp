// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Sandbox: a real app built against the public Elegence API, used to exercise features
// as they land. Only include public headers here, never anything from src/.
//
//   sandbox                  open the demo windows
//   sandbox --smoke-test     render the first frame, then exit (for automated checks)
//   sandbox --light          start in light mode

#include "palette.h"
#include "todos.h"

#include <elg/elegence.h>

#include <cstdio>
#include <format>
#include <functional>
#include <string>
#include <string_view>

namespace {

using namespace elg;

// A custom ButtonStyle: a tinted circle that deepens on hover and press.
struct PillButtonStyle : ButtonStyle {
    View make_body(const ButtonConfiguration& button) const override {
        const auto tint = Environment<TintKey>();
        return button.label.font(Font::headline)
            .foreground_style([=] { return button.is_enabled() ? tint() : tint().opacity(0.35f); })
            .frame(32, 32)
            .background(
                [=] {
                    const float strength = !button.is_enabled() ? 0.06f
                                           : button.is_pressed() ? 0.4f
                                           : button.is_hovered() ? 0.24f
                                                                 : 0.14f;
                    return tint().opacity(strength);
                },
                Capsule());
    }
};

View TitleBar(State<ColorScheme> scheme) {
    // Animated: every theme color cross-fades.
    const auto toggle = [=] {
        with_animation(Animation::ease_in_out(0.35), [=] {
            scheme = scheme.peek() == ColorScheme::dark ? ColorScheme::light : ColorScheme::dark;
        });
    };
    return HStack({.alignment = VerticalAlignment::center, .spacing = 12},
                  Text("Elegence").font(Font::title2),
                  Text("Sandbox").foreground_style(ThemeColor::secondary),
                  Spacer(),
                  Button(Text([=] { return scheme() == ColorScheme::dark ? "Light mode" : "Dark mode"; }), toggle)
                      .button_style(BorderedButtonStyle{}),
                  Circle().fill(palette::red).frame(14, 14),
                  Circle().fill(palette::peach).frame(14, 14),
                  Circle().fill(palette::green).frame(14, 14))
        .padding(Edges::horizontal, 32)
        .frame({.height = 64})
        .background(ThemeColor::surface);
}

// A shape above its caption.
View Captioned(View shape, std::string caption) {
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 12},
                  shape,
                  Text(std::move(caption)).font(Font::caption).foreground_style(ThemeColor::secondary));
}

// Three equally flexible cards share the row's width.
View ShapeCards() {
    return HStack({.spacing = 24},
                  Captioned(RoundedRectangle({.top_left = 48, .top_right = 4, .bottom_right = 48, .bottom_left = 4})
                                .fill(palette::mauve),
                            "RoundedRectangle with per-corner radii")
                      .card(),
                  Captioned(RoundedRectangle(28).stroke_border(palette::green, 6), "stroke_border(green, 6)").card(),
                  Captioned(Circle().fill(palette::sky).stroke_border(Color::white.opacity(0.85f), 3),
                            "Circle: fill + stroke_border")
                      .card())
        .frame({.height = 170});
}

// Wrapping, truncation, and alignment.
View TextCards() {
    const std::string story =
        "Elegence lays text out with HarfBuzz and draws it from a multi-channel signed distance "
        "field atlas, so the same glyphs stay sharp at every size and scale. Words wrap to the "
        "width the layout proposes, and text truncates with an ellipsis when it runs out of room.";

    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 8},
                         Text("Wrapping").font(Font::headline),
                         Text(story))
                      .frame({.max_width = infinity, .alignment = Alignment::top_leading})
                      .card(),
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 8},
                         Text("line_limit(2)").font(Font::headline),
                         Text(story).line_limit(2).foreground_style(ThemeColor::secondary))
                      .frame({.max_width = infinity, .alignment = Alignment::top_leading})
                      .card(),
                  VStack({.spacing = 6},
                         Text("Aa").font(Font::system(56, Weight::light)),
                         Text("Centered\nmulti-line").multiline_text_alignment(TextAlignment::center))
                      .frame({.max_width = infinity})
                      .card());
}

// Clipping, gradients, group opacity, and a layered shadow.
View EffectCards() {
    // Stripes that overflow their frame, clipped to a circle.
    const View stripes = HStack({.spacing = 0},
                                Rectangle().fill(palette::red),
                                Rectangle().fill(palette::peach),
                                Rectangle().fill(palette::green),
                                Rectangle().fill(palette::sky))
                             .frame(140, 140)
                             .offset(-20, -10)
                             .frame(96, 96)
                             .clip_shape(Circle());

    const View gradients = HStack({.spacing = 12},
                                  RoundedRectangle(16).fill(LinearGradient({palette::blue, palette::mauve},
                                                                           UnitPoint::top_leading,
                                                                           UnitPoint::bottom_trailing)),
                                  Circle().fill(RadialGradient({Color::white, palette::sky, palette::blue.opacity(0)},
                                                               UnitPoint::center, 0, 48)))
                               .frame({.height = 96});

    // Left: the pair fades as a group, so the overlap doesn't show through.
    // Right: each circle fades on its own, so the overlap is darker.
    const auto pair = [](float each) {
        return ZStack({.alignment = Alignment::leading},
                      Circle().fill(palette::red.opacity(each)).frame(56, 56),
                      Circle().fill(palette::blue.opacity(each)).frame(56, 56).offset(32, 0))
            .frame(88, 56, Alignment::leading);
    };
    const View opacity = HStack({.spacing = 16}, pair(1).opacity(0.55f), pair(0.55f)).frame({.height = 96});

    // A composite view (shape + text) casting one shadow: rendered as an offscreen layer.
    const View raised = VStack({.spacing = 6},
                               Circle().fill(palette::green).frame(28, 28),
                               Text("Layer").font(Font::headline))
                            .padding(14)
                            .background(ThemeColor::surface_raised, RoundedRectangle(12))
                            .shadow({.color = Color::black.opacity(0.6f), .radius = 18, .x = 0, .y = 8})
                            .frame({.height = 96});

    return HStack({.spacing = 24},
                  Captioned(stripes.frame({.max_width = infinity}), "clip_shape(Circle())").card(),
                  Captioned(gradients, "Linear + radial gradients").card(),
                  Captioned(opacity, "Group vs per-shape opacity").card(),
                  Captioned(raised.frame({.max_width = infinity}), "shadow() on a composite").card());
}

void log(const char* message) {
    std::printf("sandbox: %s\n", message);
    std::fflush(stdout);
}

// A child component that edits its parent's state through a Binding.
View Stepper(Binding<int> value) {
    return HStack({.spacing = 8},
                  Button("-", [=] { value -= 1; }).disabled([=] { return value() <= 0; }),
                  Text([=] { return std::to_string(value()); })
                      .font(Font::title3)
                      .multiline_text_alignment(TextAlignment::center)
                      .frame({.min_width = 32}),
                  Button("+", [=] { value += 1; }))
        .button_style(PillButtonStyle{});
}

// State drives the UI: each reactive argument updates exactly one property of one element.
View StateCards(std::function<void()> quit) {
    const View buttons = VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                                Button("Say hello", [] { log("hello"); }),
                                Button("Quit", std::move(quit)).tint(palette::red));

    // The count lives here; Stepper changes it through a Binding.
    const auto count = State(0);
    const View counter = VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                                Text([=] { return std::format("Count: {}", count()); }).font(Font::headline),
                                Stepper(count.binding()));

    // Hover changes the fill and the label; taps update a count.
    const auto hovered = State(false);
    const auto taps = State(0);
    const View hover = RoundedRectangle(12)
                           .fill([=](const EnvironmentValues& env) {
                               return hovered() ? palette::peach : env.color(ThemeColor::surface_raised);
                           })
                           .frame({.height = 56})
                           .overlay(Text([=] {
                               return hovered() ? std::format("Tapped {} times", taps()) : std::string("Hover or tap");
                           }).foreground_style([=](const EnvironmentValues& env) {
                               return env.color(hovered() ? ThemeColor::background : ThemeColor::primary);
                           }))
                           .animation(Animation::ease_out(0.18), hovered) // implicit: hover changes ease
                           .on_hover([=](bool inside) { hovered = inside; })
                           .on_tap_gesture([=] { taps += 1; });

    // A reactive opacity, toggled by a button whose label follows the same state.
    const auto dimmed = State(false);
    const View fade = HStack({.spacing = 16},
                             Circle().fill(palette::mauve).frame(44, 44).opacity([=] { return dimmed() ? 0.2f : 1.f; }),
                             Button(Text([=] { return dimmed() ? "Show" : "Dim"; }),
                                    [=] { with_animation(Animation::smooth(), [=] { dimmed = !dimmed(); }); }));

    return HStack({.spacing = 24},
                  Captioned(buttons, "Button + tint").card(),
                  Captioned(counter, "State + Binding").card(),
                  Captioned(hover.frame({.max_width = infinity}), "on_hover + on_tap_gesture").card(),
                  Captioned(fade, "Reactive opacity").card());
}

// Toggles in both styles, and keyboard focus: Tab / Shift+Tab move between controls, Space and
// Enter press them.
View ControlCards() {
    const auto wifi = State(true);
    const auto sounds = State(false);
    const View switches = VStack({.spacing = 10},
                                 Toggle("Wi-Fi", wifi.binding()),
                                 Toggle("Sounds", sounds.binding()).tint(palette::green),
                                 Toggle("Airplane mode", Binding<bool>::constant(false)).disabled())
                              .frame({.width = 220});

    const auto remember = State(true);
    const auto reports = State(false);
    const View checks = VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                               Toggle("Remember me", remember.binding()),
                               Toggle("Send crash reports", reports.binding()))
                            .toggle_style(CheckboxToggleStyle{});

    // A focusable view with its own keys; the button focuses it through the binding.
    const auto is_focused = State(false);
    const auto value = State(0);
    const View keys =
        VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
               Text([=] {
                   return is_focused() ? std::format("Left/Right: {}", value()) : std::string("Click or Tab here");
               })
                   .padding(EdgeInsets::symmetric(12, 8))
                   .background(ThemeColor::surface_raised, RoundedRectangle(8))
                   .focused(is_focused.binding())
                   .on_key_press([=](const KeyEvent& event) {
                       switch (event.key) {
                       case Key::left: value -= 1; return true;
                       case Key::right: value += 1; return true;
                       case Key::escape: is_focused = false; return true;
                       default: return false;
                       }
                   }),
               Button("Focus it", [=] { is_focused = true; }).button_style(BorderedButtonStyle{}));

    return HStack({.alignment = VerticalAlignment::top, .spacing = 24},
                  Captioned(switches, "Toggle (switch)").card(),
                  Captioned(checks, "Toggle (checkbox)").card(),
                  Captioned(keys, "focused + on_key_press").card());
}

View ContentView(State<ColorScheme> scheme, std::function<void()> quit) {
    return VStack({.spacing = 0},
                  TitleBar(scheme),
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 20},
                         ShapeCards(),
                         // Like SwiftUI's .fixedSize(horizontal: false, vertical: true): take the
                         // text's full height instead of an equal share against the Spacer.
                         TextCards().fixed_size(false, true),
                         EffectCards(),
                         StateCards(std::move(quit)),
                         ControlCards(),
                         Spacer())
                      .padding(32))
        .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::top})
        .background(ThemeColor::background)
        .theme(palette::theme)
        .color_scheme(scheme);
}

} // namespace

int main(int argc, char** argv) {
    bool smoke_test = false;
    bool light = false;
    for (int i = 1; i < argc; ++i) {
        smoke_test |= std::string_view(argv[i]) == "--smoke-test";
        light |= std::string_view(argv[i]) == "--light";
    }

    try {
        App app;
        // Shared by both windows: the title bar's toggle switches them together.
        const State<ColorScheme> scheme(light ? ColorScheme::light : ColorScheme::dark);
        app.window({.title = "Elegence Sandbox", .width = 1100, .height = 1040},
                   [&] {
                       if (smoke_test) {
                           app.quit(); // still renders the first frame
                       }
                       return ContentView(scheme, [&app] { app.quit(); });
                   });
        // A second window: If and ForEach.
        app.window({.title = "Elegence Todos", .width = 460, .height = 560}, [scheme] { return TodoWindow(scheme); });
        return app.run();
    } catch (const elg::Error& e) {
        std::fprintf(stderr, "sandbox: %s\n", e.what());
        return 1;
    }
}
