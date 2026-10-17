// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Animation: explicit animations (with_animation), implicit ones (.animation), transitions for
// views that appear and disappear, and layout that eases to its new place.

#include <elg/elegence.h>

#include <algorithm>
#include <random>
#include <vector>

using namespace elg;

// A banner that slides in from the top and fades out.
View BannerDemo() {
    const State<bool> shown(false);
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                  Button(Text([=] { return shown() ? "Hide banner" : "Show banner"; }),
                         [=] { with_animation(Animation::spring(), [&] { shown = !shown(); }); }),
                  If(shown,
                     Text("Saved to your library")
                         .foreground_style(ThemeColor::on_accent)
                         .padding(12)
                         .frame({.max_width = infinity})
                         .background(ThemeColor::accent, RoundedRectangle(10))
                         .transition(Transition::move(Edges::top).combined(Transition::opacity()))));
}

// Implicit: the knob animates whenever `on` changes, however it changed.
View SwitchDemo() {
    const State<bool> on(false);
    return ZStack({.alignment = Alignment::leading},
                  Capsule().fill([=] { return on() ? Color::hex(0x9ECE6A) : Color::hex(0x565F89); }),
                  Circle().fill(Color::white).frame(40, 40).padding(4).offset([=] { return on() ? 40.f : 0.f; }, 0.f))
        .frame(88, 48)
        .animation(Animation::spring(0.35, 0.7), on)
        .on_tap_gesture([=] { on = !on(); })
        .accessibility_element()
        .accessibility_role(AccessibilityRole::toggle)
        .accessibility_label("Demo switch");
}

// Layout animation: shuffling reorders the tiles, and each one eases to its new position.
View ShuffleDemo() {
    const State<std::vector<int>> tiles(std::vector<int>{1, 2, 3, 4, 5, 6});
    const auto shuffle = [=] {
        with_animation(Animation::smooth(0.5), [&] {
            tiles.update([](std::vector<int>& t) { std::ranges::shuffle(t, std::mt19937{std::random_device{}()}); });
        });
    };
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 10},
                  Button("Shuffle", shuffle).button_style(BorderedButtonStyle{}),
                  HStack(ForEach(tiles, [](int n) {
                      return Text(std::to_string(n))
                          .font(Font::title3)
                          .foreground_style(Color::white)
                          .frame(48, 48)
                          .background(Color::hex(0x7AA2F7).mix(Color::hex(0xBB9AF7), n / 6.f), RoundedRectangle(10));
                  })));
}

int main() {
    App app;
    app.window({.title = "Animation", .width = 480, .height = 420}, [] {
        return VStack({.alignment = HorizontalAlignment::leading, .spacing = 28}, BannerDemo(), SwitchDemo(),
                      ShuffleDemo())
            .padding(24)
            .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::top_leading});
    });
    return app.run();
}
