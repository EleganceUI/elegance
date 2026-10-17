// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Hello: the smallest Elegence app. One window with an icon and two lines of text.

#include <elg/elegence.h>

using namespace elg;

int main() {
    App app;
    app.window({.title = "Hello", .width = 480, .height = 320}, [] {
        return VStack({.spacing = 12},
                      Image("hand-waving").font(Font::system(48)).foreground_style(ThemeColor::accent),
                      Text("Hello, Elegence!").font(Font::title),
                      Text("A native UI in a few lines of C++.").foreground_style(ThemeColor::secondary));
    });
    return app.run();
}
