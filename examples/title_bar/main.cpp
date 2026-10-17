// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Title bar: a window without the system frame. The app draws its own title bar (dragging it
// moves the window), window controls, and a rounded, translucent panel the desktop shows
// through.

#include <elg/elegence.h>

using namespace elg;

View TitleBarView() {
    const auto window = Environment<WindowKey>();
    return HStack({.spacing = 8},
                  Image("note-pencil").foreground_style(ThemeColor::accent),
                  Text("Notes").font(Font::headline),
                  Spacer(),
                  WindowControls())
        .padding(EdgeInsets{.leading = 14})
        .frame({.height = 40})
        // Dims with the window, like a system title bar.
        .opacity([=] { return window().is_active() ? 1.f : 0.6f; })
        .window_drag_area();
}

View Notes() {
    const State<std::string> note;
    return VStack({.spacing = 0},
                  TitleBarView(),
                  Divider(),
                  TextField("Write something…", note, { .max_lines = std::numeric_limits<int>::max() })
                      .text_field_style(PlainTextFieldStyle{})
                      .padding(16)
                      .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::top_leading}))
        // A closure taking the environment mixes a theme color with transparency.
        .background([](const EnvironmentValues& env) { return env.color(ThemeColor::surface).opacity(0.9f); },
                    RoundedRectangle(12))
        .overlay(RoundedRectangle(12).stroke_border(ThemeColor::outline));
}

int main() {
    App app;
    app.window({.title = "Notes",
                .width = 520,
                .height = 360,
                .min_size = Size{320, 200},
                .title_bar = TitleBar::hidden,
                .transparent = true},
               Notes);
    return app.run();
}
