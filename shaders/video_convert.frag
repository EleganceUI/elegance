// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#version 450

// Video frames from memory to RGB (design.md §13.3): reads a packed frame's bytes and writes one
// pixel per fragment into a BGRA image the size of the frame. The same on every platform.

layout(std430, set = 0, binding = 0) readonly buffer Frame {
    uint words[];
};

layout(push_constant) uniform PushConstants {
    uint format;     // 0 BGRA, 1 NV12, 2 YUY2, 3 I420
    uint width;
    uint height;
    uint matrix;     // 0 BT.601, 1 BT.709
    uint full_range; // YUV levels 0...255 rather than 16...235
} pc;

layout(location = 0) out vec4 out_color; // gamma-encoded RGB (the image is sampled through an sRGB view)

uint byte_at(uint index) {
    return (words[index >> 2] >> ((index & 3u) * 8u)) & 0xFFu;
}

vec3 to_rgb(uint y8, uint u8, uint v8) {
    float y, u, v;
    if (pc.full_range != 0u) {
        y = float(y8) / 255.0;
        u = (float(u8) - 128.0) / 255.0;
        v = (float(v8) - 128.0) / 255.0;
    } else {
        y = (float(y8) - 16.0) / 219.0;
        u = (float(u8) - 128.0) / 224.0;
        v = (float(v8) - 128.0) / 224.0;
    }
    vec3 rgb = pc.matrix == 1u ? vec3(y + 1.5748 * v, y - 0.1873 * u - 0.4681 * v, y + 1.8556 * u)
                               : vec3(y + 1.402 * v, y - 0.344136 * u - 0.714136 * v, y + 1.772 * u);
    return clamp(rgb, 0.0, 1.0);
}

void main() {
    uvec2 p = uvec2(gl_FragCoord.xy);
    uint w = pc.width;
    uint h = pc.height;
    uint chroma_w = (w + 1u) / 2u;
    uint chroma_h = (h + 1u) / 2u;
    vec3 rgb;
    if (pc.format == 0u) {
        uint i = (p.y * w + p.x) * 4u; // B G R A
        rgb = vec3(byte_at(i + 2u), byte_at(i + 1u), byte_at(i)) / 255.0;
    } else if (pc.format == 1u) {
        uint y = byte_at(p.y * w + p.x);
        uint uv = w * h + (p.y / 2u) * (chroma_w * 2u) + (p.x / 2u) * 2u;
        rgb = to_rgb(y, byte_at(uv), byte_at(uv + 1u));
    } else if (pc.format == 2u) {
        uint base = (p.y * w + (p.x & ~1u)) * 2u; // Y0 U Y1 V
        rgb = to_rgb(byte_at(base + (p.x & 1u) * 2u), byte_at(base + 1u), byte_at(base + 3u));
    } else {
        uint y = byte_at(p.y * w + p.x);
        uint chroma = (p.y / 2u) * chroma_w + p.x / 2u;
        uint u_plane = w * h;
        uint v_plane = u_plane + chroma_w * chroma_h;
        rgb = to_rgb(y, byte_at(u_plane + chroma), byte_at(v_plane + chroma));
    }
    out_color = vec4(rgb, 1.0);
}
