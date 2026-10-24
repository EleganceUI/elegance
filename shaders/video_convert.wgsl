// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Video frames from memory to RGB (design.md §13.3): reads a packed frame's bytes and writes one
// pixel per fragment into a BGRA image the size of the frame. The same on every platform.

@group(0) @binding(0) var<storage, read> words: array<u32>;

struct Immediates {
    format: u32,     // 0 BGRA, 1 NV12, 2 YUY2, 3 I420
    width: u32,
    height: u32,
    color_matrix: u32, // 0 BT.601, 1 BT.709
    full_range: u32, // YUV levels 0...255 rather than 16...235
}
var<immediate> pc: Immediates;

// A single triangle covering the viewport.
@vertex
fn vs_main(@builtin(vertex_index) vertex: u32) -> @builtin(position) vec4f {
    let p = vec2f(f32((vertex << 1u) & 2u), f32(vertex & 2u));
    return vec4f(p * 2.0 - 1.0, 0.0, 1.0);
}

fn byte_at(index: u32) -> u32 {
    return (words[index >> 2u] >> ((index & 3u) * 8u)) & 0xFFu;
}

fn to_rgb(y_byte: u32, u_byte: u32, v_byte: u32) -> vec3f {
    var y: f32;
    var u: f32;
    var v: f32;
    if (pc.full_range != 0u) {
        y = f32(y_byte) / 255.0;
        u = (f32(u_byte) - 128.0) / 255.0;
        v = (f32(v_byte) - 128.0) / 255.0;
    } else {
        y = (f32(y_byte) - 16.0) / 219.0;
        u = (f32(u_byte) - 128.0) / 224.0;
        v = (f32(v_byte) - 128.0) / 224.0;
    }
    var rgb: vec3f;
    if (pc.color_matrix == 1u) {
        rgb = vec3f(y + 1.5748 * v, y - 0.1873 * u - 0.4681 * v, y + 1.8556 * u);
    } else {
        rgb = vec3f(y + 1.402 * v, y - 0.344136 * u - 0.714136 * v, y + 1.772 * u);
    }
    return clamp(rgb, vec3f(0.0), vec3f(1.0));
}

// Gamma-encoded RGB: the image is sampled through an sRGB view.
@fragment
fn fs_main(@builtin(position) position: vec4f) -> @location(0) vec4f {
    let p = vec2u(position.xy);
    let w = pc.width;
    let h = pc.height;
    let chroma_w = (w + 1u) / 2u;
    let chroma_h = (h + 1u) / 2u;
    var rgb: vec3f;
    if (pc.format == 0u) {
        let i = (p.y * w + p.x) * 4u; // B G R A
        rgb = vec3f(f32(byte_at(i + 2u)), f32(byte_at(i + 1u)), f32(byte_at(i))) / 255.0;
    } else if (pc.format == 1u) {
        let y = byte_at(p.y * w + p.x);
        let uv = w * h + (p.y / 2u) * (chroma_w * 2u) + (p.x / 2u) * 2u;
        rgb = to_rgb(y, byte_at(uv), byte_at(uv + 1u));
    } else if (pc.format == 2u) {
        let base = (p.y * w + (p.x & ~1u)) * 2u; // Y0 U Y1 V
        rgb = to_rgb(byte_at(base + (p.x & 1u) * 2u), byte_at(base + 1u), byte_at(base + 3u));
    } else {
        let y = byte_at(p.y * w + p.x);
        let chroma = (p.y / 2u) * chroma_w + p.x / 2u;
        let u_plane = w * h;
        let v_plane = u_plane + chroma_w * chroma_h;
        rgb = to_rgb(y, byte_at(u_plane + chroma), byte_at(v_plane + chroma));
    }
    return vec4f(rgb, 1.0);
}
