// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Sandbox: a real app built against the public Elegence API, used to exercise features
// as they land. Only include public headers here, never anything from src/.
//
//   sandbox                  open the demo window
//   sandbox --smoke-test     render the first frame, then exit (for automated checks)

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

View TitleBar() {
    return HStack({.alignment = VerticalAlignment::center, .spacing = 12},
                  Text("Elegence").font(Font::title2),
                  Text("Sandbox").foreground_style(palette::subtext),
                  Spacer(),
                  Circle().fill(palette::red).frame(14, 14),
                  Circle().fill(palette::peach).frame(14, 14),
                  Circle().fill(palette::green).frame(14, 14))
        .padding(Edges::horizontal, 32)
        .frame({.height = 64})
        .background(LinearGradient({palette::mantle, Color::hex(0x2E3250)}, UnitPoint::leading, UnitPoint::trailing));
}

// A shape above its caption.
View Captioned(View shape, std::string caption) {
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 12},
                  shape,
                  Text(std::move(caption)).font(Font::caption).foreground_style(palette::subtext));
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
                         Text(story).line_limit(2).foreground_style(palette::subtext))
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
                            .background(palette::muted, RoundedRectangle(12))
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
                  Button("-", [=] { value -= 1; }),
                  Text([=] { return std::to_string(value()); })
                      .font(Font::title3)
                      .multiline_text_alignment(TextAlignment::center)
                      .frame({.min_width = 32}),
                  Button("+", [=] { value += 1; }));
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
                           .fill([=] { return hovered() ? palette::peach : palette::muted; })
                           .frame({.height = 56})
                           .overlay(Text([=] {
                               return hovered() ? std::format("Tapped {} times", taps()) : std::string("Hover or tap");
                           }).foreground_style([=] { return hovered() ? palette::base : palette::text; }))
                           .on_hover([=](bool inside) { hovered = inside; })
                           .on_tap_gesture([=] { taps += 1; });

    // A reactive opacity, toggled by a button whose label follows the same state.
    const auto dimmed = State(false);
    const View fade = HStack({.spacing = 16},
                             Circle().fill(palette::mauve).frame(44, 44).opacity([=] { return dimmed() ? 0.2f : 1.f; }),
                             Button(Text([=] { return dimmed() ? "Show" : "Dim"; }), [=] { dimmed = !dimmed(); }));

    return HStack({.spacing = 24},
                  Captioned(buttons, "Button + tint").card(),
                  Captioned(counter, "State + Binding").card(),
                  Captioned(hover.frame({.max_width = infinity}), "on_hover + on_tap_gesture").card(),
                  Captioned(fade, "Reactive opacity").card());
}

View ContentView(std::function<void()> quit) {
    return VStack({.spacing = 0},
                  TitleBar(),
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 20},
                         ShapeCards(),
                         // Like SwiftUI's .fixedSize(horizontal: false, vertical: true): take the
                         // text's full height instead of an equal share against the Spacer.
                         TextCards().fixed_size(false, true),
                         EffectCards(),
                         StateCards(std::move(quit)),
                         Spacer())
                      .padding(32))
        .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::top})
        .foreground_style(palette::text);
}

} // namespace

int main(int argc, char** argv) {
    const bool smoke_test = argc > 1 && std::string_view(argv[1]) == "--smoke-test";

    try {
        App app;
        app.window({.title = "Elegence Sandbox", .width = 1100, .height = 900, .background = palette::base},
                   [&] {
                       if (smoke_test) {
                           app.quit(); // still renders the first frame
                       }
                       return ContentView([&app] { app.quit(); });
                   });
        // A second window: If and ForEach.
        app.window({.title = "Elegence Todos", .width = 460, .height = 560, .background = palette::base},
                   [] { return TodoWindow(); });
        return app.run();
    } catch (const elg::Error& e) {
        std::fprintf(stderr, "sandbox: %s\n", e.what());
        return 1;
    }
}
