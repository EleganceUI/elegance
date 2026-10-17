// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Recorder: voice memos from the microphone, with a live level meter, and a camera preview
// with snapshots.

#include <elg/elegence.h>

#include <format>
#include <memory>
#include <vector>

using namespace elg;

struct Memo {
    int id = 0;
    double seconds = 0;
    Sound sound;

    friend bool operator==(const Memo& a, const Memo& b) { return a.id == b.id; }
};

View Memos() {
    const AudioRecorder recorder;
    const State<std::vector<Memo>> memos;
    const State<int> next_id(1);

    const auto toggle = [=] {
        if (!recorder.is_recording()) {
            recorder.clear();
            recorder.start();
            return;
        }
        recorder.stop();
        memos.update([&](std::vector<Memo>& list) {
            list.push_back({next_id(), recorder.duration(), recorder.sound()});
        });
        next_id += 1;
    };

    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 12},
                  Text("Voice memos").font(Font::title2),
                  HStack(Button(Label("Record", {.system_name = "microphone"}), toggle)
                             .tint([=] { return recorder.is_recording() ? Color::hex(0xFF453A) : Color::hex(0x0A84FF); }),
                         ProgressView([=] { return recorder.level(); }),
                         Text([=] { return std::format("{:.1f} s", recorder.duration()); })
                             .foreground_style(ThemeColor::secondary)),
                  If([=] { return !recorder.error().empty(); },
                     Text([=] { return recorder.error(); }).foreground_style(ThemeColor::danger)),
                  ForEach(memos, &Memo::id, [](const Memo& memo) {
                      return HStack(Text(std::format("Memo {}", memo.id)),
                                    Spacer(),
                                    Text(std::format("{:.1f} s", memo.seconds)).foreground_style(ThemeColor::secondary),
                                    Button(Image("play"), [sound = memo.sound] { sound.play(); })
                                        .button_style(PlainButtonStyle{})
                                        .accessibility_label("Play"));
                  }));
}

View CameraPanel() {
    const Camera camera;
    const State<int> shots(0);
    const auto last = std::make_shared<std::vector<unsigned char>>(); // the latest snapshot's PNG

    return VStack({.alignment = HorizontalAlignment::leading, .spacing = 12},
                  Text("Camera").font(Font::title2),
                  CameraView(camera).frame(320, 180).clip_shape(RoundedRectangle(12)),
                  HStack(Button(Text([=] { return camera.is_running() ? "Stop" : "Start"; }),
                                [=] { camera.is_running() ? camera.stop() : camera.start(); }),
                         Button("Snapshot",
                                [=] {
                                    if (auto png = camera.snapshot()) {
                                        *last = std::move(*png);
                                        shots += 1;
                                    }
                                })
                             .button_style(BorderedButtonStyle{})
                             .disabled([=] { return !camera.is_running(); })),
                  // Rebuilt for each new snapshot: the shot count is the item's key.
                  ForEach([=] { return shots() ? std::vector<int>{shots()} : std::vector<int>{}; }, [=](int) {
                      return Image({.data = *last}).resizable().aspect_ratio(ContentMode::fit).frame(160, 90);
                  }),
                  If([=] { return !camera.error().empty(); },
                     Text([=] { return camera.error(); }).foreground_style(ThemeColor::danger)));
}

int main() {
    App app;
    app.window({.title = "Recorder", .width = 760, .height = 520}, [] {
        return HStack({.alignment = VerticalAlignment::top, .spacing = 32}, Memos().frame({.width = 320}), CameraPanel())
            .padding(24)
            .frame({.max_width = infinity, .max_height = infinity, .alignment = Alignment::top_leading});
    });
    return app.run();
}
