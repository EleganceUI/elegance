// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Media as the Mikros build has it: the layer built from the Mikros source list (the stand-ins for audio,
// video and cameras) in place of the real ones, linked with the layers below it only. Playing a sound,
// recording, opening a video or a camera fails with PlatformError, and the lists of devices are empty
// (ELGSPEC-0040.R21).

#include "media/video_source.h"

#include <elg/audio.h>
#include <elg/capture.h>
#include <elg/error.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace elg;
using Catch::Matchers::ContainsSubstring;

namespace {

constexpr const char* offered = "Mikros doesn't offer";

// The message of the PlatformError that `action` throws; empty if it throws nothing else.
template <typename Action>
std::string platform_error_of(Action&& action) {
    try {
        action();
    } catch (const PlatformError& error) {
        return error.what();
    }
    return {};
}

} // namespace

TEST_CASE("on Mikros, there are no audio devices or cameras", "[mikros]") {
    CHECK(audio_output_devices().empty());
    CHECK(audio_input_devices().empty());
    CHECK(camera_devices().empty());
}

TEST_CASE("on Mikros, a sound, a player and a stream can't be made", "[mikros]") {
    CHECK_THAT(platform_error_of([] { Sound::load(std::filesystem::path("a.wav")); }), ContainsSubstring(offered));
    const std::vector<unsigned char> bytes{1, 2, 3};
    CHECK_THAT(platform_error_of([&] { Sound::load(bytes); }), ContainsSubstring("audio output"));
    CHECK_THAT(platform_error_of([] { const AudioPlayer player(std::filesystem::path("a.mp3")); }),
               ContainsSubstring("audio output"));
    CHECK_THAT(platform_error_of([] { const AudioStream stream; }), ContainsSubstring("audio output"));
}

TEST_CASE("on Mikros, a recorder is made and keeps its options in range, but doesn't record", "[mikros]") {
    const AudioRecorder recorder({.sample_rate = 1, .channels = 9});
    CHECK(recorder.sample_rate() == 8000);
    CHECK(recorder.channels() == 2);
    CHECK_FALSE(recorder.is_recording());
    CHECK_THAT(recorder.error(), ContainsSubstring(offered));
    CHECK_THAT(platform_error_of([&] { recorder.start(); }), ContainsSubstring("audio recording"));
    CHECK_THAT(platform_error_of([&] { recorder.toggle(); }), ContainsSubstring("audio recording"));
    CHECK_THAT(platform_error_of([&] { (void)recorder.wav(); }), ContainsSubstring("audio recording"));
    CHECK_THAT(platform_error_of([&] { (void)recorder.sound(); }), ContainsSubstring("audio recording"));
    CHECK_THAT(platform_error_of([&] { recorder.save_wav("a.wav"); }), ContainsSubstring("audio recording"));
    CHECK_FALSE(recorder.is_recording());
    recorder.stop();
    recorder.clear();
}

TEST_CASE("on Mikros, a camera is made, and starting it fails", "[mikros]") {
    const Camera camera;
    CHECK_THAT(camera.error(), ContainsSubstring(offered));
    CHECK_THAT(platform_error_of([&] { camera.start(); }), ContainsSubstring("cameras"));
    CHECK_FALSE(camera.is_running());
    camera.stop();
}

TEST_CASE("on Mikros, a video is made, says why it can't play, and playing it fails", "[mikros]") {
    const std::shared_ptr<detail::VideoSource> video = detail::VideoSource::create("clip.mp4");
    REQUIRE(video);
    CHECK_THAT(video->error.peek(), ContainsSubstring("video playback"));
    CHECK_THAT(platform_error_of([&] { video->play(); }), ContainsSubstring("video playback"));
    CHECK_FALSE(video->playing.peek());
    video->pause();
}
