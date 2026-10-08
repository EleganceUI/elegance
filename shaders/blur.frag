// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#version 450

// One direction of a separable Gaussian blur of a single channel, for layer shadows.
// Source and target share the same valid region (in texels) at their top-left corner; the
// textures may be larger (they're pooled), and everything outside the region reads as zero.

layout(set = 0, binding = 0) uniform sampler2D source;

layout(push_constant) uniform PushConstants {
    vec2 region;    // valid texels
    vec2 direction; // (1, 0) or (0, 1)
    float sigma;    // texels
    float channel;  // 0: read alpha (layer color), 1: read red (previous blur pass)
} pc;

layout(location = 0) out vec4 out_value;

const int kMaxRadius = 64;

float read(vec2 texel) {
    if (any(lessThan(texel, vec2(0.0))) || any(greaterThanEqual(texel, pc.region))) {
        return 0.0;
    }
    vec4 s = texture(source, texel / vec2(textureSize(source, 0)));
    return pc.channel == 0.0 ? s.a : s.r;
}

void main() {
    vec2 texel = gl_FragCoord.xy; // texel centers
    float sigma = max(pc.sigma, 0.5);
    int radius = min(int(ceil(3.0 * sigma)), kMaxRadius);
    float sum = 0.0;
    float weights = 0.0;
    for (int i = -radius; i <= radius; ++i) {
        float w = exp(-0.5 * float(i * i) / (sigma * sigma));
        sum += w * read(texel + pc.direction * float(i));
        weights += w;
    }
    out_value = vec4(sum / weights);
}
