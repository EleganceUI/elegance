// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Capture (design.md §13.3). Devices differ per machine, so these check what holds everywhere:
// listing devices works, and recorders and cameras behave before they start.

#include <elg/elegence.h>

#include <catch2/catch_test_macros.hpp>

using namespace elg;

TEST_CASE("devices can be listed", "[capture]") {
    CHECK_NOTHROW(audio_input_devices());
    CHECK_NOTHROW(camera_devices());
}

TEST_CASE("a recorder before recording", "[capture]") {
    const AudioRecorder recorder({.sample_rate = 16000, .channels = 1});
    CHECK_FALSE(recorder.is_recording());
    CHECK(recorder.duration() == 0.0);
    CHECK(recorder.samples().empty());
    CHECK(recorder.sample_rate() == 16000);
    CHECK(recorder.channels() == 1);
    // An empty recording is still a valid WAV file: the 44-byte header.
    const std::vector<unsigned char> wav = recorder.wav();
    REQUIRE(wav.size() == 44);
    CHECK(std::string(wav.begin(), wav.begin() + 4) == "RIFF");
    CHECK(std::string(wav.begin() + 8, wav.begin() + 12) == "WAVE");
    recorder.stop(); // stopping what never started is fine
    recorder.clear();
}

TEST_CASE("recorder options are kept in range", "[capture]") {
    const AudioRecorder recorder({.sample_rate = 1, .channels = 9});
    CHECK(recorder.sample_rate() == 8000);
    CHECK(recorder.channels() == 2);
}

TEST_CASE("a camera before starting", "[capture]") {
    const Camera camera({.width = 640, .height = 480, .mirrored = false});
    CHECK_FALSE(camera.is_running());
    CHECK(camera.size().width == 0);
    CHECK_FALSE(camera.snapshot().has_value());
    CHECK_FALSE(camera.mirrored());
    CHECK_NOTHROW(CameraView(camera).frame(160, 90));
    CHECK_NOTHROW(CameraView(camera, ContentMode::fit));
}
