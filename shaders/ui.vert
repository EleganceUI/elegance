// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#version 450

// One instance per UI primitive, drawn as a 4-vertex triangle strip, in painter's order.
// Units are pixels of the current render target, y down. See ui.frag for the primitive kinds.
layout(location = 0) in vec4 in_rect;         // x, y, width, height
layout(location = 1) in vec4 in_shape;        // radii | atlas uv | layer texel rect
layout(location = 2) in vec4 in_color;        // linear, premultiplied
layout(location = 3) in vec4 in_border_color; // linear, premultiplied
layout(location = 4) in vec4 in_params;       // x: border width | distance range; y: kind; z: paint; w: first stop
layout(location = 5) in vec4 in_gradient;     // linear: start.xy, end.xy | radial: center.xy, r0, r1
layout(location = 6) in vec4 in_clip_rect;    // x, y, width, height
layout(location = 7) in vec4 in_clip_radii;
layout(location = 8) in vec4 in_extra;        // x: stop count; y: shadow sigma
layout(location = 9) in vec4 in_clip_scissor; // x, y, width, height: the enclosing clips' rects

layout(push_constant) uniform PushConstants {
    vec2 viewport; // pixels
} pc;

layout(location = 0) out vec2 out_local; // position relative to the rect's center
layout(location = 1) out vec2 out_uv;
layout(location = 2) flat out vec2 out_half_size;
layout(location = 3) flat out vec4 out_shape;
layout(location = 4) flat out vec4 out_color;
layout(location = 5) flat out vec4 out_border_color;
layout(location = 6) flat out vec4 out_params;
layout(location = 7) flat out vec4 out_gradient;
layout(location = 8) flat out vec4 out_clip_rect;
layout(location = 9) flat out vec4 out_clip_radii;
layout(location = 10) flat out vec4 out_extra;
layout(location = 11) flat out vec4 out_clip_scissor;

const float kRect = 0.0;
const float kBoxShadow = 4.0;

void main() {
    vec2 corner = vec2(gl_VertexIndex & 1, gl_VertexIndex >> 1); // (0,0) (1,0) (0,1) (1,1)

    // Rects need a pixel outside for anti-aliasing; box shadows extend three sigma further.
    float margin = 0.0;
    if (in_params.y == kRect) {
        margin = 1.0;
    } else if (in_params.y == kBoxShadow) {
        margin = 3.0 * in_extra.y + 1.0;
    }

    vec2 half_size = in_rect.zw * 0.5;
    vec2 center = in_rect.xy + half_size;
    vec2 local = (corner * 2.0 - 1.0) * (half_size + margin);
    vec2 position = center + local;

    // Vulkan clip space has y pointing down, matching pixel coordinates.
    gl_Position = vec4(position / pc.viewport * 2.0 - 1.0, 0.0, 1.0);

    out_local = local;
    out_uv = mix(in_shape.xy, in_shape.zw, corner);
    out_half_size = half_size;
    out_shape = in_shape;
    out_color = in_color;
    out_border_color = in_border_color;
    out_params = in_params;
    out_gradient = in_gradient;
    out_clip_rect = in_clip_rect;
    out_clip_radii = in_clip_radii;
    out_extra = in_extra;
    out_clip_scissor = in_clip_scissor;
}
