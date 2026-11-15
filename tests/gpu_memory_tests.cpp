// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// How long the renderer keeps GPU memory that frames don't use, and how much of it. Times are
// given, not read from a clock.

#include "gpu/eviction.h"
#include "gpu/texture_cache.h"
#include "gpu/video_textures.h"
#include "render/intermediate_images.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>

using namespace elg;
using namespace std::chrono_literals;

namespace {

// Stands in for gpu::RenderImage.
struct FakeImage {
    int id = 0;
    wgpu::TextureFormat format_ = wgpu::TextureFormat::RGBA8UnormSrgb;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    [[nodiscard]] wgpu::TextureFormat format() const { return format_; }
    [[nodiscard]] std::uint32_t width() const { return width_; }
    [[nodiscard]] std::uint32_t height() const { return height_; }
};

struct Pool {
    detail::IntermediateImages<FakeImage> images;
    int made = 0;

    FakeImage& acquire(detail::ImageRole role, std::uint64_t index, std::uint32_t width, std::uint32_t height,
                       wgpu::TextureFormat format = wgpu::TextureFormat::RGBA8UnormSrgb, std::uint32_t samples = 1) {
        return images.acquire({role, index}, format, width, height, samples, [&](std::uint32_t w, std::uint32_t h) {
            return FakeImage{++made, format, w, h};
        });
    }
};

const detail::gpu::CacheClock::time_point t0{};

} // namespace

TEST_CASE("an image's GPU size counts every sample", "[gpu-memory]") {
    CHECK(detail::image_bytes(wgpu::TextureFormat::R8Unorm, 100, 50, 1) == 5'000);
    CHECK(detail::image_bytes(wgpu::TextureFormat::R8Unorm, 100, 50, 4) == 20'000);
    CHECK(detail::image_bytes(wgpu::TextureFormat::Stencil8, 100, 50, 4) == 20'000);
    CHECK(detail::image_bytes(wgpu::TextureFormat::BGRA8Unorm, 100, 50, 1) == 20'000);
}

TEST_CASE("a window's offscreen images are kept by slot, whichever slot comes first", "[gpu-memory]") {
    Pool pool;
    pool.images.begin_frame(t0);
    const int fifth = pool.acquire(detail::ImageRole::layer, 5, 300, 200).id;
    const int first = pool.acquire(detail::ImageRole::layer, 0, 300, 200).id;
    CHECK(fifth != first);
    pool.images.end_frame();

    pool.images.begin_frame(t0 + 16ms);
    // Smaller: the same images. Rounded up, so a little larger fits too.
    CHECK(pool.acquire(detail::ImageRole::layer, 5, 100, 100).id == fifth);
    CHECK(pool.acquire(detail::ImageRole::layer, 0, 310, 250).id == first);
    // Larger than the image: a new one in its place, never smaller than the old one.
    const FakeImage& grown = pool.acquire(detail::ImageRole::layer, 5, 600, 10);
    CHECK(grown.id != fifth);
    CHECK(grown.width() >= 600);
    CHECK(grown.height() >= 200);
    // Another format: a new image.
    CHECK(pool.acquire(detail::ImageRole::layer, 0, 10, 10, wgpu::TextureFormat::BGRA8UnormSrgb).id != first);
    pool.images.end_frame();
    CHECK(pool.images.size() == 2);
}

TEST_CASE("an offscreen image unused for 5 seconds is released by the window's next frame", "[gpu-memory]") {
    Pool pool;
    pool.images.begin_frame(t0);
    pool.acquire(detail::ImageRole::layer, 0, 100, 100);
    pool.acquire(detail::ImageRole::path_samples, 0, 100, 100, wgpu::TextureFormat::R8Unorm, 4);
    pool.images.end_frame();

    // Frames that use only the layer.
    pool.images.begin_frame(t0 + 4s);
    pool.acquire(detail::ImageRole::layer, 0, 100, 100);
    pool.images.end_frame();
    pool.images.begin_frame(t0 + 4999ms);
    pool.acquire(detail::ImageRole::layer, 0, 100, 100);
    pool.images.end_frame();
    CHECK(pool.images.contains({detail::ImageRole::path_samples, 0}));

    pool.images.begin_frame(t0 + 5s);
    pool.acquire(detail::ImageRole::layer, 0, 100, 100);
    pool.images.end_frame();
    CHECK_FALSE(pool.images.contains({detail::ImageRole::path_samples, 0}));
    CHECK(pool.images.contains({detail::ImageRole::layer, 0}));

    // No frame for a long time: the next one releases what it doesn't use.
    pool.images.begin_frame(t0 + 60s);
    pool.images.end_frame();
    CHECK(pool.images.size() == 0);
}

TEST_CASE("a window keeps at most 64 MB of offscreen images beyond what its frame uses", "[gpu-memory]") {
    // 2048 x 2048, 4 samples of one byte: 16 MB each.
    constexpr std::uint32_t kSide = 2048;
    Pool pool;
    for (std::uint64_t i = 0; i < 5; ++i) {
        pool.images.begin_frame(t0 + i * 10ms);
        pool.acquire(detail::ImageRole::path_samples, i, kSide, kSide, wgpu::TextureFormat::R8Unorm, 4);
        pool.images.end_frame();
    }
    // Each frame used one: the other four, 64 MB, stayed.
    CHECK(pool.images.size() == 5);

    // A frame that uses all of them keeps them all, over the budget.
    pool.images.begin_frame(t0 + 100ms);
    for (std::uint64_t i = 0; i < 5; ++i) {
        pool.acquire(detail::ImageRole::path_samples, i, kSide, kSide, wgpu::TextureFormat::R8Unorm, 4);
    }
    pool.images.end_frame();
    CHECK(pool.images.size() == 5);

    // Then frames that use one each, the second a path mask too: the least recently used go first.
    pool.images.begin_frame(t0 + 200ms);
    pool.acquire(detail::ImageRole::path_samples, 3, kSide, kSide, wgpu::TextureFormat::R8Unorm, 4);
    pool.images.end_frame();
    pool.images.begin_frame(t0 + 300ms);
    pool.acquire(detail::ImageRole::path_samples, 4, kSide, kSide, wgpu::TextureFormat::R8Unorm, 4);
    pool.images.store({detail::ImageRole::path_mask, 42}, FakeImage{100, wgpu::TextureFormat::R8Unorm, 4096, 4096},
                      detail::image_bytes(wgpu::TextureFormat::R8Unorm, 4096, 4096, 1));
    pool.images.end_frame();
    // Unused now: 0, 1, 2 (16 MB each, used at 100 ms) and 3 (used at 200 ms): 64 MB, kept.
    CHECK(pool.images.size() == 6);

    pool.images.begin_frame(t0 + 400ms);
    pool.images.find({detail::ImageRole::path_mask, 42});
    pool.images.end_frame();
    // Now 4 too: 80 MB unused, one of the oldest goes.
    CHECK(pool.images.size() == 5);
    CHECK(pool.images.contains({detail::ImageRole::path_samples, 3}));
    CHECK(pool.images.contains({detail::ImageRole::path_samples, 4}));
}

TEST_CASE("GPU caches release entries by idle time and by the bytes the frame doesn't use", "[gpu-memory]") {
    struct Entry {
        detail::gpu::CacheUse use;
        bool pinned = false;
    };
    std::map<int, Entry> entries;
    detail::gpu::CacheFrame frame;
    const detail::gpu::CachePolicy policy{10s, 100};

    frame.begin(t0);
    for (int i = 0; i < 4; ++i) {
        frame.mark(entries[i].use);
        entries[i].use.bytes = 40;
    }
    entries[3].pinned = true;
    frame.begin(t0 + 1s);
    frame.mark(entries[0].use);
    const auto pinned = [](const Entry& entry) { return entry.pinned; };
    // 1 and 2 are unused (80 bytes): within the budget.
    CHECK(detail::gpu::evict(entries, frame, policy, pinned) == 80);
    CHECK(entries.size() == 4);

    frame.begin(t0 + 2s);
    entries[4].use.bytes = 40;
    frame.mark(entries[4].use);
    frame.begin(t0 + 3s);
    // 0, 1, 2 and 4 are unused (160 bytes): 1 and 2, the least recently used, go.
    CHECK(detail::gpu::evict(entries, frame, policy, pinned) == 80);
    CHECK(entries.size() == 3);
    CHECK(entries.contains(0));
    CHECK(entries.contains(3));
    CHECK(entries.contains(4));

    // Unused for 10 s: released, unless pinned.
    frame.begin(t0 + 12s);
    CHECK(detail::gpu::evict(entries, frame, policy, pinned) == 0);
    CHECK(entries.size() == 1);
    CHECK(entries.contains(3));
}

namespace {

// Entries of a GPU cache under `policy`, by name.
struct FakeCache {
    struct Entry {
        detail::gpu::CacheUse use;
    };
    detail::gpu::CachePolicy policy;
    std::map<std::string, Entry, std::less<>> entries;
    detail::gpu::CacheFrame frame;

    // A frame at `now` that draws `used` (names and bytes).
    void draw(detail::gpu::CacheClock::time_point now,
              std::initializer_list<std::pair<const char*, std::size_t>> used) {
        frame.begin(now);
        for (const auto& [name, bytes] : used) {
            Entry& entry = entries[name];
            entry.use.bytes = bytes;
            frame.mark(entry.use);
        }
        detail::gpu::evict(entries, frame, policy);
    }
};

constexpr std::size_t kMB = std::size_t{1} << 20;

} // namespace

TEST_CASE("a decoded image's texture counts its mipmaps", "[gpu-memory]") {
    // 4 x 2, then 2 x 1, then 1 x 1, four bytes a pixel.
    CHECK(detail::gpu::decoded_texture_bytes(4, 2) == 32 + 8 + 4);
    CHECK(detail::gpu::decoded_texture_bytes(1, 1) == 4);
}

TEST_CASE("decoded images' textures go once undrawn for 10 s, or past 512 MB beyond the frame's", "[gpu-memory]") {
    FakeCache cache{detail::gpu::kDecodedImagePolicy, {}, {}};
    cache.draw(t0, {{"photo", 300 * kMB}, {"banner", 300 * kMB}, {"icon", kMB}});
    CHECK(cache.entries.size() == 3); // the frame draws them all

    cache.draw(t0 + 1s, {{"icon", kMB}});
    // 600 MB undrawn: the photo or the banner goes.
    CHECK(cache.entries.size() == 2);
    CHECK(cache.entries.contains("icon"));

    // The other one was drawn last at t0, the icon at t0 + 1 s.
    cache.draw(t0 + 9999ms, {});
    CHECK(cache.entries.size() == 2);
    cache.draw(t0 + 10s, {});
    CHECK(cache.entries.size() == 1);
    CHECK(cache.entries.contains("icon"));
    cache.draw(t0 + 11s, {});
    CHECK(cache.entries.empty());
}

TEST_CASE("video textures go once undrawn for 5 s, or past 128 MB beyond the frame's", "[gpu-memory]") {
    FakeCache cache{detail::gpu::kVideoTexturePolicy, {}, {}};
    // Four 4K videos (32 MB a frame) and a small one.
    cache.draw(t0, {{"a", 32 * kMB}, {"b", 32 * kMB}, {"c", 32 * kMB}, {"d", 32 * kMB}, {"small", kMB}});
    cache.draw(t0 + 1s, {{"small", kMB}});
    CHECK(cache.entries.size() == 5); // 128 MB undrawn: kept
    cache.draw(t0 + 2s, {{"e", 32 * kMB}});
    // 129 MB undrawn: one of the four drawn at t0 goes.
    CHECK(cache.entries.size() == 5);
    CHECK(cache.entries.contains("small"));
    CHECK(cache.entries.contains("e"));

    cache.draw(t0 + 4999ms, {{"e", 32 * kMB}});
    CHECK(cache.entries.size() == 5);
    cache.draw(t0 + 5s, {{"e", 32 * kMB}});
    CHECK(cache.entries.size() == 2); // the rest of what t0 drew went
    cache.draw(t0 + 6s, {{"e", 32 * kMB}});
    CHECK(cache.entries.size() == 1);
    CHECK(cache.entries.contains("e"));
}
