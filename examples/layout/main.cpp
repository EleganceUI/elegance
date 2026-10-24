// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Layout: stacks, spacers, frames, overlays, and a custom layout (a flow of tags that wraps
// to the next row when it runs out of width).

#include <elg/elegance.h>

#include <algorithm>
#include <span>
#include <string>
#include <vector>

using namespace elg;

// A custom layout: children left to right, wrapping into rows. Used like a stack:
// FlowLayout(6)(a, b, c) or FlowLayout()(std::vector<View>{...}).
class FlowLayout : public Layout {
public:
    explicit FlowLayout(float spacing = 8) : spacing_(spacing) {}

    Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override {
        return arrange(proposal.width.value_or(infinity), children, nullptr);
    }

    void place_children(Rect bounds, ProposedSize, std::span<const LayoutChild> children) const override {
        arrange(bounds.width, children, &bounds);
    }

private:
    // Measures the rows for `max_width`, placing the children inside `bounds` if given.
    Size arrange(float max_width, std::span<const LayoutChild> children, const Rect* bounds) const {
        float x = 0;
        float y = 0;
        float row_height = 0;
        float width = 0;
        for (const LayoutChild& child : children) {
            const Size size = child.size_that_fits(ProposedSize::unspecified()); // its ideal size
            if (x > 0 && x + size.width > max_width) {
                x = 0;
                y += row_height + spacing_;
                row_height = 0;
            }
            if (bounds) {
                child.place({bounds->x + x, bounds->y + y}, ProposedSize{size.width, size.height});
            }
            x += size.width + spacing_;
            width = std::max(width, x - spacing_);
            row_height = std::max(row_height, size.height);
        }
        return {width, y + row_height};
    }

    float spacing_;
};

View Tag(std::string title) {
    return Text(std::move(title))
        .font(Font::callout)
        .padding(EdgeInsets::symmetric(10, 4))
        .background(ThemeColor::surface_raised, Capsule());
}

View ProfileCard() {
    std::vector<View> tags;
    for (const char* tag : {"C++23", "WebGPU", "UI", "Reactive", "Layout", "Animation", "Accessibility", "Media",
                            "Desktop", "Open source"}) {
        tags.push_back(Tag(tag));
    }

    const View avatar = Circle()
                            .fill(LinearGradient({Color::hex(0x7AA2F7), Color::hex(0xBB9AF7)}))
                            .frame(64, 64)
                            .overlay(Text("EL").font(Font::title2).foreground_style(Color::white));

    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 16},
                  // An HStack with a Spacer pushes the badge to the trailing edge.
                  HStack({.spacing = 14},
                         avatar,
                         VStack({.alignment = HorizontalAlignment::leading, .spacing = 2},
                                Text("Elegance").font(Font::title2),
                                Text("SwiftUI-style UI for C++").foreground_style(ThemeColor::secondary)),
                         Spacer(),
                         Text("PRO")
                             .font(Font::caption.bold())
                             .foreground_style(ThemeColor::on_accent)
                             .padding(EdgeInsets::symmetric(8, 3))
                             .background(ThemeColor::accent, Capsule())),
                  Divider(),
                  FlowLayout(6)(std::move(tags)),
                  // Equal-width buttons: each label takes all it's offered, so they share the row.
                  HStack(Button(Text("Follow").frame({.max_width = infinity}), [] {}),
                         Button(Text("Message").frame({.max_width = infinity}), [] {})
                             .button_style(BorderedButtonStyle{})))
        .padding(20)
        .background(ThemeColor::surface, RoundedRectangle(16))
        .frame({.max_width = 420});
}

int main() {
    App app;
    app.window({.title = "Layout", .width = 560, .height = 440}, [] { return ProfileCard().padding(24); });
    return app.run();
}
