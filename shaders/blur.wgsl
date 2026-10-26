// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// One direction of a separable Gaussian blur (fs_main), and a box downsample (fs_downsample) that
// lets wide blurs run at a lower resolution. They serve layer shadows and inner shadows (one
// channel), and blurred layers and backdrops (color).
// The source's valid region (in texels) is at its top-left corner; the textures may be larger
// (they're pooled). Outside the region reads as transparent, or with `clamp_edges` as the nearest
// texel inside it (backdrops, opaque blurs: edges shouldn't darken).
// The blur takes neighbouring taps two at a time: one linearly filtered sample between them,
// placed by their weights, gives their weighted sum (half the reads).

@group(0) @binding(0) var source: texture_2d<f32>;
@group(0) @binding(1) var source_sampler: sampler; // linear

struct Immediates {
    region: vec2f,    // valid source texels
    direction: vec2f, // (1, 0) or (0, 1)
    sigma: f32,       // texels
    mode: f32,        // 0: alpha -> red, 1: red -> red, 2: color, 3: 1 - alpha -> red
    clamp_edges: f32, // 1: outside the region repeats its edge
    factor: f32,      // fs_downsample: source texels per target texel, each way
}
var<immediate> pc: Immediates;

const kMaxRadius = 64;

// A single triangle covering the viewport.
@vertex
fn vs_main(@builtin(vertex_index) vertex: u32) -> @builtin(position) vec4f {
    let p = vec2f(f32((vertex << 1u) & 2u), f32(vertex & 2u)); // (0,0) (2,0) (0,2)
    return vec4f(p * 2.0 - 1.0, 0.0, 1.0);
}

// What a source color contributes, per mode. Affine in the color, so it applies as well to a
// filtered mix of texels as to each of them.
fn channel(s: vec4f) -> vec4f {
    if (pc.mode == 0.0) {
        return vec4f(s.a);
    }
    if (pc.mode == 1.0) {
        return vec4f(s.r);
    }
    if (pc.mode == 3.0) {
        return vec4f(1.0 - s.a);
    }
    return s;
}

fn inside(texel: vec2f) -> bool {
    return all(texel >= vec2f(0.0)) && all(texel < pc.region);
}

fn read(texel: vec2f) -> vec4f {
    var t = texel;
    if (!inside(t)) {
        if (pc.clamp_edges == 0.0) {
            // Nothing there: transparent, so its inverse alpha is full.
            return channel(vec4f(0.0));
        }
        t = clamp(t, vec2f(0.0), pc.region - 1.0);
    }
    return channel(textureLoad(source, vec2i(t), 0));
}

// The taps `offset` and `offset + step` texels along the direction, weighted `wa` and `wb`: one
// filtered sample between them when both are inside the region (beyond it the pooled texture
// holds stale texels), else each read on its own.
fn read_pair(texel: vec2f, offset: f32, step: f32, wa: f32, wb: f32) -> vec4f {
    let a = texel + pc.direction * offset;
    let b = a + pc.direction * step;
    if (inside(a) && inside(b)) {
        // At the texel centers across, so only these two texels mix.
        let at = a + 0.5 + pc.direction * (step * wb / (wa + wb));
        let s = textureSampleLevel(source, source_sampler, at / vec2f(textureDimensions(source)), 0.0);
        return (wa + wb) * channel(s);
    }
    return wa * read(a) + wb * read(b);
}

@fragment
fn fs_main(@builtin(position) position: vec4f) -> @location(0) vec4f {
    let texel = floor(position.xy); // the target texel; the source is the same size
    let sigma = max(pc.sigma, 0.5);
    let radius = min(i32(ceil(3.0 * sigma)), kMaxRadius);
    let weight = 0.5 / (sigma * sigma);
    var sum = read(texel); // the center, weight 1
    var weights = 1.0;
    // Out from the center on both sides: taps (1, 2), (3, 4), ..., the last one alone when
    // `radius` is odd.
    for (var i = 1; i <= radius; i += 2) {
        let wa = exp(-weight * f32(i * i));
        if (i == radius) {
            let d = pc.direction * f32(i);
            sum += wa * (read(texel + d) + read(texel - d));
            weights += 2.0 * wa;
        } else {
            let wb = exp(-weight * f32((i + 1) * (i + 1)));
            sum += read_pair(texel, f32(i), 1.0, wa, wb) + read_pair(texel, -f32(i), -1.0, wa, wb);
            weights += 2.0 * (wa + wb);
        }
    }
    return sum / weights;
}

// The average of the factor x factor source texels under each target texel.
@fragment
fn fs_downsample(@builtin(position) position: vec4f) -> @location(0) vec4f {
    let f = i32(pc.factor);
    let base = floor(position.xy) * pc.factor;
    var sum = vec4f(0.0);
    for (var y = 0; y < f; y++) {
        for (var x = 0; x < f; x++) {
            sum += read(base + vec2f(f32(x), f32(y)));
        }
    }
    return sum / f32(f * f);
}
