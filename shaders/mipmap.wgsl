// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// One mip level of an image from the level above, with linear filtering (WebGPU has no blit).
// The source is a view of just that level; the target is the next level, half its size.

@group(0) @binding(0) var source: texture_2d<f32>;
@group(0) @binding(1) var source_sampler: sampler;

// A single triangle covering the viewport.
@vertex
fn vs_main(@builtin(vertex_index) vertex: u32) -> @builtin(position) vec4f {
    let p = vec2f(f32((vertex << 1u) & 2u), f32(vertex & 2u));
    return vec4f(p * 2.0 - 1.0, 0.0, 1.0);
}

@fragment
fn fs_main(@builtin(position) position: vec4f) -> @location(0) vec4f {
    let target_size = max(textureDimensions(source) / 2u, vec2u(1u));
    return textureSampleLevel(source, source_sampler, position.xy / vec2f(target_size), 0.0);
}
