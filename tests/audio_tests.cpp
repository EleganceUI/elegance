// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Audio loading (design.md §13.3). Playback itself needs an output device, so these check
// decoding and errors; without a device, playing is a no-op.

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace elg;

namespace {

// A WAV file in memory: 16-bit mono PCM.
std::vector<unsigned char> wav(int rate, int frames) {
    const auto put32 = [](std::vector<unsigned char>& out, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(v >> (8 * i)));
    };
    const auto put16 = [](std::vector<unsigned char>& out, std::uint16_t v) {
        out.push_back(static_cast<unsigned char>(v));
        out.push_back(static_cast<unsigned char>(v >> 8));
    };
    std::vector<unsigned char> out;
    const auto tag = [&](const char* t) { out.insert(out.end(), t, t + 4); };
    tag("RIFF");
    put32(out, 36 + frames * 2);
    tag("WAVE");
    tag("fmt ");
    put32(out, 16);
    put16(out, 1); // PCM
    put16(out, 1); // mono
    put32(out, static_cast<std::uint32_t>(rate));
    put32(out, static_cast<std::uint32_t>(rate * 2));
    put16(out, 2);
    put16(out, 16);
    tag("data");
    put32(out, static_cast<std::uint32_t>(frames * 2));
    for (int i = 0; i < frames; ++i) {
        put16(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(8000 * std::sin(i * 0.1))));
    }
    return out;
}

} // namespace

TEST_CASE("sounds decode from memory", "[audio]") {
    const std::vector<unsigned char> data = wav(22050, 22050 / 2); // half a second
    const Sound sound = Sound::load(data);
    CHECK(std::abs(sound.duration() - 0.5) < 0.01);
    CHECK_NOTHROW(sound.play({.volume = 0}));
}

TEST_CASE("audio errors", "[audio]") {
    CHECK_THROWS_AS(Sound::load("no-such-file.wav"), ResourceError);
    const std::vector<unsigned char> garbage(100, 7);
    CHECK_THROWS_AS(Sound::load(garbage), ResourceError);
    CHECK_THROWS_AS(AudioPlayer("no-such-file.mp3"), ResourceError);
}
