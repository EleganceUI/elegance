// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The lock-free sample ring between the audio device's thread and the others.

#include "media/sample_ring.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <thread>
#include <vector>

using namespace elg;

namespace {

std::vector<float> read_all(detail::SampleRing& ring) {
    std::vector<float> out;
    std::array<float, 3> chunk{};
    for (std::size_t taken = 0; (taken = ring.read(chunk)) > 0;) {
        out.insert(out.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(taken));
    }
    return out;
}

} // namespace

TEST_CASE("samples come out of the ring in the order they went in, around its end", "[audio]") {
    detail::SampleRing ring(4);
    ring.write(std::vector<float>{1, 2, 3});
    CHECK(read_all(ring) == std::vector<float>{1, 2, 3});
    ring.write(std::vector<float>{4, 5, 6}); // wraps
    CHECK(ring.size() == 3);
    CHECK(read_all(ring) == std::vector<float>{4, 5, 6});
    CHECK(ring.size() == 0);
}

TEST_CASE("a full ring drops its oldest samples", "[audio]") {
    detail::SampleRing ring(4);
    ring.write(std::vector<float>{1, 2, 3});
    ring.write(std::vector<float>{4, 5});
    CHECK(ring.size() == 4);
    CHECK(read_all(ring) == std::vector<float>{2, 3, 4, 5});
    ring.write(std::vector<float>{6, 7, 8, 9, 10, 11}); // more than it holds: the newest
    CHECK(read_all(ring) == std::vector<float>{8, 9, 10, 11});
}

TEST_CASE("clearing a ring drops what's waiting", "[audio]") {
    detail::SampleRing ring(8);
    ring.write(std::vector<float>{1, 2, 3});
    ring.clear();
    CHECK(ring.size() == 0);
    ring.write(std::vector<float>{4});
    CHECK(read_all(ring) == std::vector<float>{4});
}

TEST_CASE("a ring passes every sample between two threads when it has room", "[audio][threads]") {
    constexpr int kSamples = 200'000;
    detail::SampleRing ring(kSamples);
    std::thread writer([&] {
        std::vector<float> chunk;
        for (int next = 0; next < kSamples;) {
            chunk.clear();
            for (int i = 0; i < 97 && next < kSamples; ++i) {
                chunk.push_back(static_cast<float>(next++));
            }
            ring.write(chunk);
        }
    });
    std::vector<float> got;
    got.reserve(kSamples);
    std::array<float, 64> chunk{};
    while (got.size() < static_cast<std::size_t>(kSamples)) {
        const std::size_t taken = ring.read(chunk);
        got.insert(got.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(taken));
    }
    writer.join();
    bool in_order = true;
    for (std::size_t i = 0; i < got.size(); ++i) {
        in_order = in_order && got[i] == static_cast<float>(i);
    }
    CHECK(in_order);
}
