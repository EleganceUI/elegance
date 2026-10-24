// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Path coverage masks, in three steps: stencil (vs_fill), cover (vs_cover + fs_coverage), and
// resolve (vs_cover + fs_resolve).

struct Immediates {
    viewport: vec2f, // the mask's size in pixels
}
var<immediate> pc: Immediates;

// The cover step's multisampled coverage, for the resolve step.
@group(0) @binding(0) var samples: texture_multisampled_2d<f32>;

// Step 1 (stencil): a path's fan triangles, in mask pixels (y down).
@vertex
fn vs_fill(@location(0) position: vec2f) -> @builtin(position) vec4f {
    let ndc = position / pc.viewport * 2.0 - 1.0;
    return vec4f(ndc.x, -ndc.y, 0.0, 1.0);
}

// Steps 2 and 3: one triangle over the whole viewport.
@vertex
fn vs_cover(@builtin(vertex_index) vertex: u32) -> @builtin(position) vec4f {
    let corner = vec2f(f32((vertex << 1u) & 2u), f32(vertex & 2u));
    return vec4f(corner * 2.0 - 1.0, 0.0, 1.0);
}

// Step 2 (cover): full coverage per sample, wherever the stencil test says "inside".
@fragment
fn fs_coverage() -> @location(0) vec4f {
    return vec4f(1.0);
}

// Step 3 (resolve): the average of a pixel's samples is its anti-aliased coverage. Done in a
// shader rather than with a resolve target, which would have to be the size of the (pooled,
// larger) multisampled image.
@fragment
fn fs_resolve(@builtin(position) position: vec4f) -> @location(0) vec4f {
    let pixel = vec2i(position.xy);
    let count = textureNumSamples(samples);
    var sum = 0.0;
    for (var i = 0u; i < count; i++) {
        sum += textureLoad(samples, pixel, i).r;
    }
    return vec4f(sum / f32(count));
}
