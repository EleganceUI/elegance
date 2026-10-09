// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// A views module for hot reload (design.md §9.6): edit this file while elegence-preview (or
// hot_reload_demo) runs, save, and the views rebuild and reload in place.

#include <elg/elegence.h>

#include <format>
#include <string>

using namespace elg;

namespace {

constexpr Color kBlue = Color::hex(0x8AADF4);
constexpr Color kMauve = Color::hex(0xC6A0F6);
constexpr Color kGreen = Color::hex(0xA6DA95);
constexpr Color kPeach = Color::hex(0xF5A97F);

View Avatar(std::string initials, Color color) {
    return Circle()
        .fill(color.opacity(0.85f))
        .frame(52, 52)
        .overlay(Text(std::move(initials)).font(Font::system(20, Weight::semibold)).foreground_style(Color::white));
}

View ProfileCard(std::string name, std::string role, Color color) {
    std::string initials;
    for (std::size_t i = 0; i < name.size(); ++i) {
        if (i == 0 || name[i - 1] == ' ') {
            initials += name[i];
        }
    }
    return HStack({.spacing = 14}, Avatar(initials, color),
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 2},
                         Text(std::move(name)).font(Font::headline),
                         Text(std::move(role)).foreground_style(ThemeColor::secondary)),
                  Spacer(), Image("caret-right").foreground_style(ThemeColor::tertiary))
        .padding(16)
        .frame({.width = 320})
        .background(RoundedRectangle(14).fill(ThemeColor::surface));
}

View StatTile(std::string title, std::string value, Color color) {
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 4},
                  Text(std::move(title)).font(Font::caption).foreground_style(ThemeColor::secondary),
                  Text(std::move(value)).font(Font::system(26, Weight::bold)).foreground_style(color))
        .padding(14)
        .frame({.width = 140, .alignment = Alignment::leading})
        .background(RoundedRectangle(12).fill(ThemeColor::surface));
}

// A counter: state lives in the module, so it resets on reload (design.md §9.6).
View Counter() {
    const State<int> count(0);
    return HStack({.spacing = 12}, Button("−", [=] { count -= 1; }),
                  Text([=] { return std::format("{}", count()); }).font(Font::title2).frame({.width = 60}),
                  Button("+", [=] { count += 1; }))
        .button_style(BorderedButtonStyle{});
}

View Gallery() {
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 18},
                  Text("Hot reload gallery").font(Font::large_title),
                  Text("Edit apps/preview/gallery/gallery.cpp and save.").foreground_style(ThemeColor::secondary),
                  ProfileCard("Ada Lovelace", "Analyst", kMauve), ProfileCard("Grace Hopper", "Rear admiral", kBlue),
                  HStack({.spacing = 12}, StatTile("Builds", "128", kGreen), StatTile("Reloads", "42", kPeach)),
                  Counter())
        .padding(32)
        .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::top_leading});
}

} // namespace

ELG_MODULE(Gallery)

ELG_PREVIEW("Profile card") {
    return ProfileCard("Ada Lovelace", "Analyst", kMauve);
}

ELG_PREVIEW("Profile card, light") {
    return ProfileCard("Grace Hopper", "Rear admiral", kBlue).padding(24).background(ThemeColor::background).color_scheme(
        ColorScheme::light);
}

ELG_PREVIEW("Stat tiles") {
    return HStack({.spacing = 12}, StatTile("Builds", "128", kGreen), StatTile("Reloads", "42", kPeach),
                  StatTile("Errors", "0", kBlue));
}

ELG_PREVIEW("Counter") {
    return Counter();
}

ELG_PREVIEW("Whole gallery") {
    return Gallery();
}
