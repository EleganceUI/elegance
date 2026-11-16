// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// What the renderer keeps on the GPU across frames: how long and how much of what frames don't
// use, and what it doesn't prepare again; and how large its textures get. Times are given, not
// read from a clock.

#include "gpu/device.h"
#include "gpu/eviction.h"
#include "gpu/external_image.h"
#include "gpu/instance.h"
#include "gpu/texture_cache.h"
#include "gpu/video_textures.h"
#include "gpu/webgpu.h"
#include "render/frame_plan.h"
#include "render/intermediate_images.h"
#include "render/path_cache.h"
#include "view/view_node.h"

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>
#include <webgpu/webgpu_cpp.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

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
    std::uint32_t max_side = 8192; // the device's
    int made = 0;

    FakeImage& acquire(detail::ImageRole role, std::uint64_t index, std::uint32_t width, std::uint32_t height,
                       wgpu::TextureFormat format = wgpu::TextureFormat::RGBA8UnormSrgb, std::uint32_t samples = 1) {
        return images.acquire({role, index}, format, width, height, samples, max_side,
                              [&](std::uint32_t w, std::uint32_t h) { return FakeImage{++made, format, w, h}; });
    }
};

const detail::gpu::CacheClock::time_point t0{};

} // namespace

TEST_CASE("an image's GPU size counts every sample", "[gpu-memory]") {
    CHECK(detail::image_bytes(wgpu::TextureFormat::R8Unorm, 100, 50, 1) == 5'000);
    CHECK(detail::image_bytes(wgpu::TextureFormat::R8Unorm, 100, 50, 4) == 20'000);
    // Stencil as backends may store it: 32 bits.
    CHECK(detail::image_bytes(wgpu::TextureFormat::Stencil8, 100, 50, 4) == 80'000);
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

TEST_CASE("the decoded image policy: 10 s undrawn, or past 512 MB beyond the frame's", "[gpu-memory]") {
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

TEST_CASE("the video texture policy: 5 s undrawn, or past 128 MB beyond the frame's", "[gpu-memory]") {
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

namespace {

// A display list that fills `polygons` (in a 10 x 10 box) at `x`, 5.
detail::DisplayList path_at(const std::shared_ptr<const detail::Polygons>& polygons, float x) {
    detail::DisplayList list;
    list.commands.emplace_back(detail::PathCommand{polygons, {0, 0, 10, 10}, 1, {x, 5}, {x, 5, 10, 10},
                                                   ShapeStyle(Color::black), FillRule::non_zero});
    return list;
}

} // namespace

TEST_CASE("a path drawn again unchanged is not hashed again", "[gpu-memory]") {
    const auto square = std::make_shared<const detail::Polygons>(detail::Polygons{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}});
    detail::PathHashes hashes;
    const detail::FramePlan first = detail::plan_frame(path_at(square, 5), 100, 100, &hashes);
    REQUIRE(first.paths.size() == 1);
    CHECK(hashes.hashed() == 1);

    // Moved by whole pixels in the next frame: the same mask, from what the first frame hashed.
    const detail::FramePlan moved = detail::plan_frame(path_at(square, 7), 100, 100, &hashes);
    REQUIRE(moved.paths.size() == 1);
    CHECK(hashes.hashed() == 1);
    CHECK(moved.paths[0].key == first.paths[0].key);
    // Half a pixel further: another mask.
    CHECK(detail::plan_frame(path_at(square, 7.5f), 100, 100, &hashes).paths.at(0).key != first.paths[0].key);
    CHECK(hashes.hashed() == 1);

    // The same points in a set of their own: hashed, the same mask.
    const auto copy = std::make_shared<const detail::Polygons>(*square);
    CHECK(detail::plan_frame(path_at(copy, 5), 100, 100, &hashes).paths.at(0).key == first.paths[0].key);
    CHECK(hashes.hashed() == 2);

    // That frame didn't draw the square: it is hashed again.
    detail::plan_frame(path_at(square, 5), 100, 100, &hashes);
    CHECK(hashes.hashed() == 3);
}

TEST_CASE("a path's fan triangles are built from its plan, in mask pixels", "[gpu-memory]") {
    const auto square = std::make_shared<const detail::Polygons>(detail::Polygons{{{0, 0}, {10, 0}, {10, 10}, {0, 10}}});
    const detail::FramePlan plan = detail::plan_frame(path_at(square, 5.25f), 100, 100);
    REQUIRE(plan.paths.size() == 1);
    const detail::PlanPath& path = plan.paths[0];
    std::vector<std::array<float, 2>> vertices;
    detail::append_path_fan(path, vertices);
    // Two triangles from the first point.
    REQUIRE(vertices.size() == 6);
    const auto [dx, dy] = path.offset;
    CHECK(dx - std::floor(dx) == 0.25f); // the quarter pixel stays in the mask
    CHECK(vertices[0] == std::array<float, 2>{dx, dy});
    CHECK(vertices[1] == std::array<float, 2>{10 * path.scale + dx, dy});
    CHECK(vertices[3] == vertices[0]);
    CHECK(vertices[5] == std::array<float, 2>{dx, 10 * path.scale + dy});
}

namespace {

// The polygon sets of the paths in `list`, in order.
std::vector<std::shared_ptr<const detail::Polygons>> polygons_in(const detail::DisplayList& list) {
    std::vector<std::shared_ptr<const detail::Polygons>> found;
    for (const detail::DrawCommand& command : list.commands) {
        if (const auto* path = std::get_if<detail::PathCommand>(&command)) {
            found.push_back(path->polygons);
        }
    }
    return found;
}

} // namespace

TEST_CASE("a path a canvas draws again unchanged is not flattened again", "[gpu-memory]") {
    detail::PathCache cache;
    const Path circle = Path().add_circle({50, 50}, 40);
    // A frame of a canvas that fills and strokes the circle, then fills `other`, at `scale`.
    const auto frame = [&](const Path& other, float scale) {
        detail::DisplayList list;
        {
            const detail::PathCache::Use use(cache);
            GraphicsContext g = detail::GraphicsAccess::context(list, {10, 10, 100, 100}, scale);
            g.fill_path(circle, ShapeStyle(Color::black));
            g.stroke_path(circle, Color::white, {.width = 2});
            g.fill_path(other, ShapeStyle(Color::black));
        }
        cache.end_frame();
        return polygons_in(list);
    };
    const Path square = Path().add_rect({0, 0, 10, 10});
    const auto first = frame(square, 2);
    REQUIRE(first.size() == 3);
    CHECK(cache.flattened() == 3);

    // The same paths at the same scale: the same polygon sets, nothing flattened.
    CHECK(frame(Path().add_rect({0, 0, 10, 10}), 2) == first);
    CHECK(cache.flattened() == 3);
    // Another square: only it is flattened.
    const auto other = frame(Path().add_rect({0, 0, 20, 10}), 2);
    CHECK(cache.flattened() == 4);
    CHECK(other[0] == first[0]);
    CHECK(other[1] == first[1]);
    CHECK(other[2] != first[2]);
    // At another scale (finer flattening), all of them again.
    frame(square, 3);
    CHECK(cache.flattened() == 7);
    // The scale-2 paths weren't drawn in the frame before: forgotten.
    frame(square, 2);
    CHECK(cache.flattened() == 10);
}

TEST_CASE("a Canvas view's paths are flattened once while they stay the same", "[gpu-memory]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    detail::PathCache cache;
    const detail::PathCache::Use paths(cache);
    const auto root = detail::lower(Canvas([](GraphicsContext& g) {
                                        g.fill_path(Path().add_circle({20, 20}, 10), ShapeStyle(Color::black));
                                    }),
                                    {});
    // Lowering runs the drawing to see what it reads: no path is flattened for that.
    CHECK(cache.flattened() == 0);
    root->size_that_fits({100.f, 50.f});
    root->place({}, {100.f, 50.f});
    for (int i = 0; i < 3; ++i) {
        detail::DisplayList list;
        list.scale = 2;
        root->paint(list);
        cache.end_frame();
        CHECK(polygons_in(list).size() == 1);
    }
    CHECK(cache.flattened() == 1);
}

TEST_CASE("offscreen images are no larger than the device's textures", "[gpu-memory]") {
    Pool pool;
    pool.max_side = 1000;
    pool.images.begin_frame(t0);
    const FakeImage& image = pool.acquire(detail::ImageRole::layer, 0, 900, 1000);
    CHECK(image.width() == 1000); // rounded up only as far as the device allows
    CHECK(image.height() == 1000);
    CHECK(pool.acquire(detail::ImageRole::layer, 0, 990, 990).id == image.id);
}

TEST_CASE("layers and path masks are planned no larger than the device's textures", "[gpu-memory]") {
    const auto wide = std::make_shared<const detail::Polygons>(detail::Polygons{{{0, 0}, {9000, 0}, {9000, 10}}});
    detail::DisplayList list;
    list.commands.emplace_back(detail::PushLayerCommand{.bounds = {0, 0, 10000, 10}, .opacity = 0.5f});
    list.commands.emplace_back(detail::RectCommand{.rect = {0, 0, 10000, 10}, .fill = ShapeStyle(Color::black)});
    list.commands.emplace_back(detail::PopLayerCommand{});
    list.commands.emplace_back(detail::PathCommand{wide, {0, 0, 9000, 10}, 1, {}, {0, 0, 9000, 10},
                                                   ShapeStyle(Color::black), FillRule::non_zero});

    const detail::FramePlan limited = detail::plan_frame(list, 100, 100, nullptr, 4096);
    REQUIRE(limited.layers.size() == 1);
    REQUIRE(limited.paths.size() == 1);
    CHECK(limited.layers[0].width == 4096);
    CHECK(limited.paths[0].width == 4096);

    // A device with larger textures: 8192 at most still.
    const detail::FramePlan large = detail::plan_frame(list, 100, 100, nullptr, 16384);
    REQUIRE(large.layers.size() == 1);
    REQUIRE(large.paths.size() == 1);
    CHECK(large.layers[0].width == 8192);
    CHECK(large.paths[0].width == 8192);
}

TEST_CASE("an image larger than the device's textures gets no texture", "[.gpu][gpu-memory]") {
    const detail::gpu::Instance instance;
    const detail::gpu::Device device(instance);
    const std::uint32_t side = device.max_texture_side();
    CHECK(side >= 8192); // decode_image()'s default maximum side fits
    detail::gpu::TextureCache textures(device, detail::gpu::create_image_layout(device.handle()));
    const wgpu::CommandEncoder encoder = device.handle().CreateCommandEncoder();
    textures.begin_frame(detail::gpu::CacheClock::now());

    detail::DecodedImage image;
    image.id = 1;
    image.width = side + 1;
    image.height = 1;
    image.rgba.resize(std::size_t{image.width} * 4);
    CHECK_FALSE(textures.acquire(encoder, image));
    image.id = 2;
    image.width = 1;
    image.height = side + 1;
    CHECK_FALSE(textures.acquire(encoder, image));
    image.id = 3;
    image.height = 16;
    image.rgba.resize(std::size_t{16} * 4);
    CHECK(textures.acquire(encoder, image));

    textures.end_frame();
    device.submit(encoder.Finish());
    device.wait_idle();
}

TEST_CASE("a video frame larger than the device's textures gets no texture", "[.gpu][gpu-memory]") {
    const detail::gpu::Instance instance;
    const detail::gpu::Device device(instance);
    detail::gpu::VideoTextures videos(device, detail::gpu::create_image_layout(device.handle()));
    const wgpu::CommandEncoder encoder = device.handle().CreateCommandEncoder();
    videos.begin_frame(detail::gpu::CacheClock::now());

    detail::VideoFrame frame;
    frame.kind = detail::VideoFrame::Kind::cpu;
    frame.format = detail::VideoFrame::Format::bgra;
    frame.source_id = 1;
    frame.frame_id = 1;
    frame.width = device.max_texture_side() + 1;
    frame.height = 1;
    frame.pixels = std::make_shared<const std::vector<std::uint8_t>>(
        detail::packed_size(frame.format, frame.width, frame.height));
    std::vector<std::uint64_t> acquired;
    CHECK_FALSE(videos.acquire(encoder, frame, acquired));
    frame.source_id = 2;
    frame.width = 16;
    frame.height = 16;
    frame.pixels = std::make_shared<const std::vector<std::uint8_t>>(
        detail::packed_size(frame.format, frame.width, frame.height));
    CHECK(videos.acquire(encoder, frame, acquired));

    videos.end_frame();
    device.submit(encoder.Finish());
    videos.release(acquired);
    device.wait_idle();
}

TEST_CASE("one eviction spans several caches: their spare bytes add up", "[gpu-memory]") {
    struct Entry {
        detail::gpu::CacheUse use;
    };
    std::map<int, Entry> textures;
    std::map<int, Entry> targets;
    detail::gpu::CacheFrame frame;
    const detail::gpu::CachePolicy policy{10s, 100};
    // Drawn one a frame, oldest first: texture 1, target 2, texture 3, target 4, 40 bytes each.
    for (int i = 1; i <= 4; ++i) {
        frame.begin(t0 + i * 1s);
        Entry& entry = (i % 2 ? textures : targets)[i];
        entry.use.bytes = 40;
        frame.mark(entry.use);
    }
    frame.begin(t0 + 5s);
    detail::gpu::Eviction eviction(frame, policy);
    eviction.collect(textures);
    eviction.collect(targets);
    // 160 bytes unused, 100 allowed: the two oldest go, one from each map.
    CHECK(eviction.finish() == 80);
    CHECK(textures.size() == 1);
    CHECK(textures.contains(3));
    CHECK(targets.size() == 1);
    CHECK(targets.contains(4));
}

namespace {

// An image of `side` x `side` pixels.
detail::DecodedImage square_image(std::uint64_t id, std::uint32_t side) {
    detail::DecodedImage image;
    image.id = id;
    image.width = side;
    image.height = side;
    image.rgba.resize(std::size_t{side} * side * 4);
    return image;
}

// An image imported from another API, as GpuImage makes them: here, a texture of the test's.
class TestExternalImage final : public detail::gpu::ExternalImage {
public:
    TestExternalImage(wgpu::Device device, wgpu::Texture texture)
        : device_(std::move(device)), texture_(std::move(texture)), view_(texture_.CreateView()) {}
    void accept() noexcept override {}
    [[nodiscard]] const wgpu::Device& device() const noexcept override { return device_; }
    [[nodiscard]] const wgpu::Texture& texture() const noexcept override { return texture_; }
    [[nodiscard]] const wgpu::TextureView& view() const noexcept override { return view_; }

private:
    wgpu::Device device_;
    wgpu::Texture texture_;
    wgpu::TextureView view_;
};

// A frame in memory of `side` x `side` pixels from video source `source`.
detail::VideoFrame cpu_frame(std::uint64_t source, std::uint32_t side) {
    detail::VideoFrame frame;
    frame.kind = detail::VideoFrame::Kind::cpu;
    frame.format = detail::VideoFrame::Format::bgra;
    frame.source_id = source;
    frame.frame_id = source;
    frame.width = side;
    frame.height = side;
    frame.pixels = std::make_shared<const std::vector<std::uint8_t>>(detail::packed_size(frame.format, side, side));
    return frame;
}

} // namespace

TEST_CASE("TextureCache releases a texture no frame drew for 10 s", "[.gpu][gpu-memory]") {
    const detail::gpu::Instance instance;
    const detail::gpu::Device device(instance);
    detail::gpu::TextureCache textures(device, detail::gpu::create_image_layout(device.handle()));
    const detail::DecodedImage image = square_image(1, 16);
    const wgpu::CommandEncoder encoder = device.handle().CreateCommandEncoder();
    const auto frame = [&](detail::gpu::CacheClock::duration at, bool draw) {
        textures.begin_frame(t0 + at);
        if (draw) {
            CHECK(textures.acquire(encoder, image));
        }
        textures.end_frame();
    };
    frame(0s, true);
    frame(9s, true); // drawn again: its 10 s start over
    frame(18900ms, false);
    CHECK(textures.size() == 1);
    frame(19s, false);
    CHECK(textures.size() == 0);
    device.submit(encoder.Finish());
    device.wait_idle();
}

TEST_CASE("TextureCache keeps the textures a frame didn't draw within its budget", "[.gpu][gpu-memory]") {
    const detail::gpu::Instance instance;
    const detail::gpu::Device device(instance);
    // Room for two 64 x 64 images beyond the frame's.
    detail::gpu::TextureCache textures(device, detail::gpu::create_image_layout(device.handle()),
                                       {10s, 2 * detail::gpu::decoded_texture_bytes(64, 64)});
    std::vector<detail::DecodedImage> images;
    for (std::uint64_t id = 1; id <= 4; ++id) {
        images.push_back(square_image(id, 64));
    }
    const wgpu::CommandEncoder encoder = device.handle().CreateCommandEncoder();
    std::vector<wgpu::BindGroup> groups; // held, so a new group can't take an old one's address
    for (std::size_t i = 0; i < images.size(); ++i) {
        textures.begin_frame(t0 + i * 1s);
        groups.push_back(textures.acquire(encoder, images[i]));
        textures.end_frame();
    }
    // Three undrawn after the last frame: the first, the least recently drawn, went.
    CHECK(textures.size() == 3);
    textures.begin_frame(t0 + 5s);
    CHECK(textures.acquire(encoder, images[1]).Get() == groups[1].Get()); // kept
    CHECK(textures.acquire(encoder, images[0]).Get() != groups[0].Get()); // uploaded again
    textures.end_frame();
    // A frame that draws all four keeps them, beyond the budget.
    textures.begin_frame(t0 + 6s);
    for (const detail::DecodedImage& image : images) {
        textures.acquire(encoder, image);
    }
    textures.end_frame();
    CHECK(textures.size() == 4);
    device.submit(encoder.Finish());
    device.wait_idle();
}

TEST_CASE("VideoTextures releases a frame's texture no frame drew for 5 s", "[.gpu][gpu-memory]") {
    const detail::gpu::Instance instance;
    const detail::gpu::Device device(instance);
    detail::gpu::VideoTextures videos(device, detail::gpu::create_image_layout(device.handle()));
    const detail::VideoFrame video = cpu_frame(1, 16);
    const wgpu::CommandEncoder encoder = device.handle().CreateCommandEncoder();
    std::vector<std::uint64_t> acquired;
    const auto frame = [&](detail::gpu::CacheClock::duration at, bool draw) {
        videos.begin_frame(t0 + at);
        if (draw) {
            CHECK(videos.acquire(encoder, video, acquired));
        }
        videos.end_frame();
    };
    frame(0s, true);
    frame(3s, true); // drawn again: its 5 s start over
    frame(7900ms, false);
    CHECK(videos.size() == 1);
    frame(8s, false);
    CHECK(videos.size() == 0);
    device.submit(encoder.Finish());
    device.wait_idle();
}

TEST_CASE("VideoTextures keeps the textures a frame didn't draw within its budget", "[.gpu][gpu-memory]") {
    const detail::gpu::Instance instance;
    const detail::gpu::Device device(instance);
    // Room for two 64 x 64 frames beyond the frame's.
    detail::gpu::VideoTextures videos(device, detail::gpu::create_image_layout(device.handle()),
                                      {5s, std::size_t{2} * 64 * 64 * 4});
    const wgpu::CommandEncoder encoder = device.handle().CreateCommandEncoder();
    std::vector<std::uint64_t> acquired;
    for (std::uint64_t source = 1; source <= 4; ++source) {
        videos.begin_frame(t0 + source * 100ms);
        CHECK(videos.acquire(encoder, cpu_frame(source, 64), acquired));
        videos.end_frame();
    }
    CHECK(videos.size() == 3); // the first source's went
    device.submit(encoder.Finish());
    device.wait_idle();
}

TEST_CASE("VideoTextures keeps an external image while a recording holds it", "[.gpu][gpu-memory]") {
    const detail::gpu::Instance instance;
    const detail::gpu::Device device(instance);
    detail::gpu::VideoTextures videos(device, detail::gpu::create_image_layout(device.handle()));
    wgpu::TextureDescriptor descriptor;
    descriptor.size = {16, 16, 1};
    descriptor.format = wgpu::TextureFormat::RGBA8UnormSrgb;
    descriptor.usage = wgpu::TextureUsage::TextureBinding;
    detail::VideoFrame frame;
    frame.kind = detail::VideoFrame::Kind::external;
    frame.width = 16;
    frame.height = 16;
    frame.texture_id = 7;
    frame.external = std::make_shared<TestExternalImage>(device.handle(), device.handle().CreateTexture(&descriptor));
    const wgpu::CommandEncoder encoder = device.handle().CreateCommandEncoder();

    std::vector<std::uint64_t> acquired;
    videos.begin_frame(t0);
    CHECK(videos.acquire(encoder, frame, acquired));
    videos.end_frame();
    // Not released yet, long past 5 s undrawn: still held.
    videos.begin_frame(t0 + 60s);
    videos.end_frame();
    CHECK(videos.size() == 1);
    device.submit(encoder.Finish());
    videos.release(acquired);
    videos.begin_frame(t0 + 61s);
    videos.end_frame();
    CHECK(videos.size() == 0);
    device.wait_idle();
}
