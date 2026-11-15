// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_INTERMEDIATE_IMAGES_H
#define ELG_RENDER_INTERMEDIATE_IMAGES_H

#include "gpu/eviction.h"
#include "gpu/webgpu.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace elg::detail {

// What one of a window's offscreen images is for.
enum class ImageRole : std::uint8_t {
    layer,             // a layer's content, by layer index
    shadow_horizontal, // a layer shadow's first blur pass, by shadow
    shadow,            // a layer's blurred shadow, by shadow
    inner_horizontal,  // an inner shadow's first blur pass, by inner shadow
    inner_shadow,      // a layer's blurred inner shadow, by inner shadow
    color_down,        // a color blur's downsampled input, by blur
    color_horizontal,  // a color blur's first pass, by blur
    color_blurred,     // a color blur's result, by blur
    backdrop_copy,     // a backdrop copied out of a layer
    window_copy,       // a backdrop copied out of the window's image (in its format)
    window,            // the window, drawn offscreen
    path_samples,      // a path mask's multisampled coverage
    path_stencil,      // a path mask's multisampled stencil
    path_mask,         // a path's coverage mask, by PlanPath::key
};

// One image: its role and, where a role has several, which.
struct ImageSlot {
    ImageRole role{};
    std::uint64_t index = 0;
    bool operator==(const ImageSlot&) const = default;
};

struct ImageSlotHash {
    std::size_t operator()(const ImageSlot& slot) const noexcept {
        return std::hash<std::uint64_t>{}(slot.index * 31 + static_cast<std::uint64_t>(slot.role));
    }
};

// The GPU memory an image takes: every sample of every texel.
constexpr std::size_t image_bytes(wgpu::TextureFormat format, std::uint32_t width, std::uint32_t height,
                                  std::uint32_t samples) {
    std::size_t texel = 16; // formats not listed: at most this
    switch (format) {
    case wgpu::TextureFormat::R8Unorm:
    case wgpu::TextureFormat::Stencil8: texel = 1; break;
    case wgpu::TextureFormat::RGBA8Unorm:
    case wgpu::TextureFormat::RGBA8UnormSrgb:
    case wgpu::TextureFormat::BGRA8Unorm:
    case wgpu::TextureFormat::BGRA8UnormSrgb: texel = 4; break;
    default: break;
    }
    return texel * width * height * std::max(samples, 1u);
}

inline constexpr gpu::CachePolicy kIntermediateImagePolicy{std::chrono::seconds(5), std::size_t{64} << 20};

// A window's offscreen images (layers, blur passes, backdrop and window copies, path masks and
// their multisampled scratch), kept across frames for reuse. Each frame begins with
// begin_frame(); end_frame() then releases images that no frame used for
// kIntermediateImagePolicy.idle_limit, and the least recently used while those this frame didn't
// use take more than kIntermediateImagePolicy.spare_bytes. `Image` has width(), height() and
// format(). Render thread only.
template <class Image>
class IntermediateImages {
public:
    struct Entry {
        Image image;
        wgpu::BindGroup group; // path masks: the group that samples it
        gpu::CacheUse use;
    };

    void begin_frame(gpu::CacheClock::time_point now) noexcept { frame_.begin(now); }

    // The image in `slot`, now used by this frame, or null.
    Entry* find(const ImageSlot& slot) {
        const auto it = images_.find(slot);
        if (it == images_.end()) {
            return nullptr;
        }
        frame_.mark(it->second.use);
        return &it->second;
    }

    // Puts `image`, which takes `bytes`, in `slot` in place of what was there; used by this frame.
    Entry& store(const ImageSlot& slot, Image image, std::size_t bytes, wgpu::BindGroup group = {}) {
        Entry& entry = images_.insert_or_assign(slot, Entry{std::move(image), std::move(group), {}}).first->second;
        entry.use.bytes = bytes;
        frame_.mark(entry.use);
        return entry;
    }

    // The image in `slot` when it has `format` and at least `width` x `height` pixels. Otherwise
    // `make(width, height)` makes one in its place, rounded up so that small changes (animations,
    // resizes) reuse it, and never smaller than the one it replaces.
    template <class Make>
    Image& acquire(const ImageSlot& slot, wgpu::TextureFormat format, std::uint32_t width, std::uint32_t height,
                   std::uint32_t samples, const Make& make) {
        Entry* entry = find(slot);
        if (entry && entry->image.format() == format && entry->image.width() >= width &&
            entry->image.height() >= height) {
            return entry->image;
        }
        const auto capacity = [](std::uint32_t side) { return (side + 255) / 256 * 256; };
        std::uint32_t capacity_width = capacity(width);
        std::uint32_t capacity_height = capacity(height);
        if (entry && entry->image.format() == format) {
            capacity_width = std::max(capacity_width, entry->image.width());
            capacity_height = std::max(capacity_height, entry->image.height());
        }
        return store(slot, make(capacity_width, capacity_height),
                     image_bytes(format, capacity_width, capacity_height, samples))
            .image;
    }

    void release(const ImageSlot& slot) { images_.erase(slot); }

    // Releases what kIntermediateImagePolicy says to, now that the frame's images are known.
    void end_frame() { gpu::evict(images_, frame_, kIntermediateImagePolicy); }

    [[nodiscard]] bool contains(const ImageSlot& slot) const { return images_.contains(slot); }
    [[nodiscard]] std::size_t size() const noexcept { return images_.size(); }

private:
    gpu::CacheFrame frame_;
    std::unordered_map<ImageSlot, Entry, ImageSlotHash> images_;
};

} // namespace elg::detail

#endif // ELG_RENDER_INTERMEDIATE_IMAGES_H
