// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#version 450

// Path coverage, step 1 (stencil): a path's fan triangles, in mask pixels.
layout(location = 0) in vec2 in_position;

layout(push_constant) uniform PushConstants {
    vec2 viewport; // the mask's size in pixels
} pc;

void main() {
    gl_Position = vec4(in_position / pc.viewport * 2.0 - 1.0, 0.0, 1.0);
}
