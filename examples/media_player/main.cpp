// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Media player: open a video with the system's file dialog (or drop one on the window) and
// play it with the built-in controls.

#include <elg/elegance.h>

#include <filesystem>
#include <functional>
#include <vector>

using namespace elg;

// The video being shown. A ForEach keyed by `id` builds a new player for each file opened.
struct Opened {
    int id = 0;
    std::filesystem::path file;

    friend bool operator==(const Opened&, const Opened&) = default;
};

View Screen(const std::filesystem::path& file) {
    const VideoPlayer player(file);
    player.play();
    return ZStack({.alignment = Alignment::bottom},
                  VideoView(player).frame({.max_width = infinity, .max_height = infinity}),
                  VideoControls(player).padding(12),
                  If([=] { return !player.error().empty(); },
                     Text([=] { return player.error(); }).foreground_style(Color::white).padding(24)));
}

View Placeholder(std::function<void()> open) {
    return VStack({.spacing = 12},
                  Image("film-strip").font(Font::system(48)).foreground_style(ThemeColor::tertiary),
                  Text("Drop a video here").font(Font::title3),
                  Button("Open video…", std::move(open)));
}

View Player() {
    const State<std::vector<Opened>> opened; // empty, or the one video
    const State<int> next_id(1);
    const State<bool> targeted(false);

    const auto show = [=](std::filesystem::path file) {
        opened = std::vector<Opened>{{next_id(), std::move(file)}};
        next_id += 1;
    };
    const auto open = [=] {
        choose_files({.filters = {{"Videos", {"mp4", "mov", "m4v", "wmv", "avi", "mkv", "webm"}}}},
                     [=](std::vector<std::filesystem::path> files) {
                         if (!files.empty()) {
                             show(files.front());
                         }
                     });
    };

    return ZStack(If([=] { return opened().empty(); }, Placeholder(open)),
                  ForEach(opened, &Opened::id, [](const Opened& video) { return Screen(video.file); }))
        .frame({.max_width = infinity, .max_height = infinity})
        .background([=] { return targeted() ? Color::hex(0x24283B) : Color::hex(0x16161E); })
        .foreground_style(Color::hex(0xC0CAF5))
        .on_drop({.files =
                      [=](const std::vector<std::filesystem::path>& files, Point) {
                          show(files.front());
                          return true;
                      },
                  .targeted = [=](bool on) { targeted = on; }});
}

int main() {
    App app;
    app.window({.title = "Media Player", .width = 960, .height = 600, .min_size = Size{480, 320}}, Player);
    return app.run();
}
