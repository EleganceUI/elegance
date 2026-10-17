// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Network: a request as reactive state (http::Fetch), and images loaded from the web in a long,
// lazily built list (AsyncImage).

#include <elg/elegence.h>

#include <format>
#include <vector>

using namespace elg;

// A quote from GitHub's API, with a button to fetch another.
View Quote() {
    const http::Fetch zen({.url = "https://api.github.com/zen", .headers = {{"User-Agent", "elegence-example"}}});
    return HStack({.spacing = 12},
                  Image("quotes").font(Font::title2).foreground_style(ThemeColor::accent),
                  If([=] { return zen.is_loading() && !zen.has_response(); },
                     ProgressView().frame({.width = 120}),
                     Text([=] { return zen.ok() ? zen.response().text() : zen.error(); }).font(Font::title3)),
                  Spacer(),
                  Button(Image("arrow-clockwise"), [=] { zen.reload(); })
                      .button_style(BorderedButtonStyle{})
                      .accessibility_label("Another quote"))
        .padding(16)
        .background(ThemeColor::surface, RoundedRectangle(12));
}

View Photo(int id) {
    return HStack({.spacing = 14},
                  AsyncImage(std::format("https://picsum.photos/id/{}/240/160", id),
                             {.content_mode = ContentMode::fill})
                      .frame(120, 80)
                      .clip_shape(RoundedRectangle(8)),
                  VStack({.alignment = HorizontalAlignment::leading, .spacing = 4},
                         Text(std::format("Photo #{}", id)).font(Font::headline),
                         Text("picsum.photos").foreground_style(ThemeColor::secondary)));
}

View Feed() {
    std::vector<int> ids;
    for (int id = 10; id < 210; ++id) {
        ids.push_back(id);
    }
    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 16},
                  Quote(),
                  // Only the rows near the visible part are built, so only their images load.
                  List(ForEach(ids, [](int id) { return Photo(id); })))
        .padding(20);
}

int main() {
    App app;
    app.window({.title = "Network", .width = 560, .height = 640}, Feed);
    return app.run();
}
