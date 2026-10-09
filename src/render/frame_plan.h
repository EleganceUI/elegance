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

// A run of instances drawn with one image bound: the glyph atlas, or (for layer composites)
// a layer's texture or its blurred shadow.
struct PlanSegment {
    std::uint32_t first = 0;
    std::uint32_t count = 0;
    int layer = -1;      // >= 0: a composite of that layer
    bool shadow = false; // the layer's blurred shadow rather than its content
    int path = -1;       // >= 0: a path filled through that path's coverage mask
};

// A path's coverage mask to render before the passes that use it (gpu::PathRenderer).
struct PlanPath {
    std::uint32_t width = 0; // pixels
    std::uint32_t height = 0;
    std::uint32_t first_vertex = 0; // fan triangles in FramePlan::path_vertices, mask pixels
    std::uint32_t vertex_count = 0;
    bool even_odd = false;
};

struct PlanPass {
    std::uint32_t width = 0;  // pixels (layers only)
    std::uint32_t height = 0;
    std::optional<float> shadow_sigma; // pixels; set when the layer casts a shadow
    std::vector<PlanSegment> segments;
};

// A frame turned into GPU work, independent of any Vulkan object.
struct FramePlan {
    // Offscreen layers in render order: every layer comes after the layers it contains.
    std::vector<PlanPass> layers;
    PlanPass main;
    std::vector<gpu::QuadInstance> instances;
    // Gradient stops, two entries each: sRGB straight-alpha color, then (location, 0, 0, 0).
    std::vector<std::array<float, 4>> stops;
    std::vector<PlanPath> paths;
    std::vector<std::array<float, 2>> path_vertices;
};

// Converts logical points to pixels, resolves the clip stack per instance (culling instances
// that are fully clipped), splits layers into their own passes, and collects gradient stops.
FramePlan plan_frame(const DisplayList& list);

} // namespace elg::detail

#endif // ELG_RENDER_FRAME_PLAN_H
