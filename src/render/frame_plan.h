// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_FRAME_PLAN_H
#define ELG_RENDER_FRAME_PLAN_H

#include "gpu/quad_renderer.h"
#include "render/display_list.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace elg::detail {

// Which of a layer's images a composite samples.
enum class LayerImage {
    content,      // what it drew (blurred, when the layer blurs)
    shadow,       // its blurred alpha
    inner_shadow, // its blurred inverse alpha; the content binds as the second image
};

// A run of instances drawn with one image bound: the glyph atlas, or (for layer composites)
// a layer's texture or its blurred shadow.
struct PlanSegment {
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    int layer = -1; // >= 0: a composite of that layer
    LayerImage layer_image = LayerImage::content;
    int path = -1;     // >= 0: a path filled through that path's coverage mask
    bool mask = false; // keeps only what's under the coverage or layer ("destination-in")
    int image = -1;    // >= 0: a raster image (FramePlan::images)
    int video = -1;    // >= 0: a video's current frame (FramePlan::videos)
    // >= 0: the instances read what the pass has drawn so far, snapshotted (FramePlan::backdrops)
    // just before them: blend modes and materials.
    int backdrop = -1;
};

// A path's coverage mask to render before the passes that use it (gpu::PathRenderer).
struct PlanPath {
    std::uint32_t width = 0; // pixels
    std::uint32_t height = 0;
    std::uint32_t first_vertex = 0; // fan triangles in FramePlan::path_vertices, mask pixels
    std::uint32_t vertex_count = 0;
    bool even_odd = false;
    // Identifies the mask's content (geometry, scale, sub-pixel position, size, fill rule): equal
    // keys render identical masks, so a mask can be reused across frames.
    std::uint64_t key = 0;
};

// A copy of part of a pass, taken partway through it, blurred if `sigma` is set. Instances map
// pass pixels into it with the region's origin and the downsampling factor.
struct PlanBackdrop {
    std::uint32_t x = 0; // pass pixels
    std::uint32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float sigma = 0;              // pixels of the pass; 0: no blur
    std::uint32_t downsample = 1; // the blur runs at 1/downsample the resolution
};

struct PlanPass {
    std::uint32_t width = 0;  // pixels
    std::uint32_t height = 0;
    std::optional<float> shadow_sigma; // pixels; set when the layer casts a shadow
    // A blurred layer: its content is blurred (at 1/blur_downsample the resolution) before it's
    // composited. `blur_clamp`: edges repeat the nearest pixel instead of fading out.
    std::optional<float> blur_sigma;
    std::uint32_t blur_downsample = 1;
    bool blur_clamp = false;
    std::optional<float> inner_shadow_sigma; // pixels; set when the layer has an inner shadow
    // Some segment reads a backdrop: the pass is split there (the window then draws offscreen).
    bool reads_backdrop = false;
    std::vector<PlanSegment> segments;
};

// A frame turned into GPU work, independent of any GPU object.
struct FramePlan {
    // Offscreen layers in render order: every layer comes after the layers it contains.
    std::vector<PlanPass> layers;
    PlanPass main;
    std::vector<gpu::QuadInstance> instances;
    // Gradient stops, two entries each: sRGB straight-alpha color, then (location, 0, 0, 0).
    // Color matrices too, six entries each (see shaders/ui.wgsl).
    std::vector<std::array<float, 4>> stops;
    std::vector<PlanPath> paths;
    std::vector<std::array<float, 2>> path_vertices;
    // Images drawn this frame (uploaded first if they aren't on the GPU yet).
    std::vector<std::shared_ptr<const DecodedImage>> images;
    // Videos drawn this frame.
    std::vector<std::shared_ptr<VideoSource>> videos;
    std::vector<PlanBackdrop> backdrops;
    // When the main pass reads a backdrop it draws offscreen; this instance copies the result to
    // the window.
    std::optional<std::uint32_t> blit_instance;
};

// Converts logical points to pixels, resolves the clip stack per instance (culling instances
// that are fully clipped), splits layers into their own passes, and collects gradient stops.
// `width` x `height` is the window in pixels.
FramePlan plan_frame(const DisplayList& list, std::uint32_t width, std::uint32_t height);

} // namespace elg::detail

#endif // ELG_RENDER_FRAME_PLAN_H
