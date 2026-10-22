// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Counter: state and reactive arguments. Writing `count` updates exactly the views whose
// closures read it; nothing else is rebuilt.

#include <elg/elegence.h>

#include <format>
#include <iostream>

using namespace elg;

View Counter() {
    const State<int> count(0);

    return VStack({.spacing = 16},
                  Text([=] { return std::format("{}", count()); }).font(Font::system(56, Weight::bold)),
                  HStack(Button("−", [=] { count -= 1; }),
                         Button("Reset", [=] { count = 0; })
                             .button_style(BorderedButtonStyle{})
                             .disabled([=] { return count() == 0; }),
                         Button("+", [=] { count += 1; })),
                  Text([=] { return count() % 2 == 0 ? "even" : "odd"; })
                      .foreground_style(ThemeColor::secondary));
}

int main() {
    App app;

    app.window({.title = "Counter", .width = 360, .height = 280}, Counter);
    auto res = app.run();

    return res;
}
