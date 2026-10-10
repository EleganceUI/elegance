// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#version 450

// The glyph atlas (MTSDF), or for layer composites, the layer's texture.
layout(set = 0, binding = 0) uniform sampler2D image;

// Gradient stops, two entries each: sRGB color with straight alpha, then (location, 0, 0, 0).
layout(std430, set = 1, binding = 0) readonly buffer GradientStops {
    vec4 stops[];
};

layout(location = 0) in vec2 in_local;
layout(location = 1) in vec2 in_uv;
layout(location = 2) flat in vec2 in_half_size;
layout(location = 3) flat in vec4 in_shape;
layout(location = 4) flat in vec4 in_color;
layout(location = 5) flat in vec4 in_border_color;
layout(location = 6) flat in vec4 in_params;
layout(location = 7) flat in vec4 in_gradient;
layout(location = 8) flat in vec4 in_clip_rect;
layout(location = 9) flat in vec4 in_clip_radii;
layout(location = 10) flat in vec4 in_extra;
layout(location = 11) flat in vec4 in_clip_scissor;

layout(location = 0) out vec4 out_color; // premultiplied

// Primitive kinds (in_params.y).
const float kRect = 0.0;        // rounded rect: in_shape = radii, fill (in_params.z paint), border
const float kGlyph = 1.0;       // MSDF glyph: in_uv = atlas uv, in_params.x = distance range in px
const float kLayer = 2.0;       // a layer's texture: in_uv = texels, in_color.a = opacity
const float kLayerShadow = 3.0; // a layer's blurred alpha (R8): in_uv = texels, in_color = shadow
const float kBoxShadow = 4.0;   // analytic blurred rounded rect: in_extra.y = sigma
const float kPath = 5.0;        // a path's coverage mask (R8): in_uv = texels; fill as for rects
const float kImage = 6.0;       // a raster image (sRGB, straight alpha): in_uv normalized, in_color.a = opacity

// Paints (in_params.z).
const float kSolid = 0.0;
const float kLinear = 1.0;
const float kRadial = 2.0;

// Signed distance in pixels to a rounded box centered at the origin, negative inside.
// radii = (top-left, top-right, bottom-right, bottom-left); y points down.
float rounded_box_distance(vec2 p, vec2 half_size, vec4 radii) {
    float r = p.x > 0.0 ? (p.y > 0.0 ? radii.z : radii.y)
                        : (p.y > 0.0 ? radii.w : radii.x);
    vec2 q = abs(p) - half_size + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float median(float a, float b, float c) {
    return max(min(a, b), min(max(a, b), c));
}

vec3 srgb_to_linear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c));
}

// Premultiplied sRGB -> premultiplied linear.
vec4 to_linear(vec4 premultiplied) {
    float a = premultiplied.a;
    vec3 straight = a > 0.0 ? premultiplied.rgb / a : vec3(0.0);
    return vec4(srgb_to_linear(straight) * a, a);
}

vec4 stop_color(int index) {
    vec4 c = stops[2 * index];
    return vec4(c.rgb * c.a, c.a);
}

// Interpolates stops in sRGB with premultiplied alpha, as CSS does.
vec4 gradient_color(float t) {
    int first = int(in_params.w);
    int count = int(in_extra.x);
    t = clamp(t, 0.0, 1.0);
    vec4 previous = stop_color(first);
    float previous_location = stops[2 * first + 1].x;
    if (t <= previous_location) {
        return to_linear(previous);
    }
    for (int i = 1; i < count; ++i) {
        vec4 color = stop_color(first + i);
        float location = stops[2 * (first + i) + 1].x;
        if (t <= location) {
            float f = (t - previous_location) / max(location - previous_location, 1e-6);
            return to_linear(mix(previous, color, f));
        }
        previous = color;
        previous_location = location;
    }
    return to_linear(previous);
}

vec4 fill_color() {
    vec2 p = gl_FragCoord.xy;
    if (in_params.z == kLinear) {
        vec2 start = in_gradient.xy;
        vec2 direction = in_gradient.zw - start;
        return gradient_color(dot(p - start, direction) / max(dot(direction, direction), 1e-6));
    }
    if (in_params.z == kRadial) {
        float distance = length(p - in_gradient.xy);
        return gradient_color((distance - in_gradient.z) / max(in_gradient.w - in_gradient.z, 1e-6));
    }
    return in_color;
}

vec4 shade_rect() {
    float d = rounded_box_distance(in_local, in_half_size, in_shape);
    // Analytic anti-aliasing: coverage ramps over one pixel centered on the edge.
    float coverage = clamp(0.5 - d, 0.0, 1.0);

    vec4 color = fill_color();
    float border_width = in_params.x;
    if (border_width > 0.0) {
        // Offsetting the distance gives the inner edge, with radii shrunk by the border width.
        float inside_border = clamp(0.5 - (d + border_width), 0.0, 1.0);
        color = mix(in_border_color, color, inside_border);
    }
    return color * coverage;
}

vec4 shade_glyph() {
    vec4 field = texture(image, in_uv);
    // 0.5 is the glyph edge; scale the field's units to screen pixels for a 1px AA ramp.
    float distance = median(field.r, field.g, field.b) - 0.5;
    float screen_range = max(in_params.x, 1.0);
    float coverage = clamp(distance * screen_range + 0.5, 0.0, 1.0);
    return in_color * coverage;
}

// --- Analytic rounded-rect shadow (Evan Wallace, "Fast rounded rectangle shadows") ---

vec2 erf(vec2 x) {
    vec2 s = sign(x), a = abs(x);
    x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}

float gaussian(float x, float sigma) {
    const float pi = 3.141592653589793;
    return exp(-(x * x) / (2.0 * sigma * sigma)) / (sqrt(2.0 * pi) * sigma);
}

float rounded_box_shadow_x(float x, float y, float sigma, float corner, vec2 half_size) {
    float delta = min(half_size.y - corner - abs(y), 0.0);
    float curved = half_size.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    vec2 integral = 0.5 + 0.5 * erf((x + vec2(-curved, curved)) * (sqrt(0.5) / sigma));
    return integral.y - integral.x;
}

float rounded_box_shadow(vec2 point, vec2 half_size, float sigma, float corner) {
    float low = point.y - half_size.y;
    float high = point.y + half_size.y;
    float start = clamp(-3.0 * sigma, low, high);
    float end = clamp(3.0 * sigma, low, high);
    float step_size = (end - start) / 4.0;
    float y = start + step_size * 0.5;
    float value = 0.0;
    for (int i = 0; i < 4; ++i) {
        value += rounded_box_shadow_x(point.x, point.y - y, sigma, corner, half_size) * gaussian(y, sigma) * step_size;
        y += step_size;
    }
    return value;
}

vec4 shade_box_shadow() {
    float sigma = max(in_extra.y, 0.5);
    float corner = max(max(in_shape.x, in_shape.y), max(in_shape.z, in_shape.w));
    corner = min(corner, min(in_half_size.x, in_half_size.y));
    return in_color * rounded_box_shadow(in_local, in_half_size, sigma, corner);
}

// --- Clipping ---

// The innermost rounded clip, at its own size, times the enclosing clips' rect: a circle half
// scrolled out of view stays a circle with a straight cut.
float clip_coverage() {
    vec2 half_size = in_clip_rect.zw * 0.5;
    vec2 center = in_clip_rect.xy + half_size;
    float shape = clamp(0.5 - rounded_box_distance(gl_FragCoord.xy - center, half_size, in_clip_radii), 0.0, 1.0);
    vec2 scissor_half = in_clip_scissor.zw * 0.5;
    vec2 scissor_center = in_clip_scissor.xy + scissor_half;
    float scissor = clamp(0.5 - rounded_box_distance(gl_FragCoord.xy - scissor_center, scissor_half, vec4(0.0)), 0.0, 1.0);
    return shape * scissor;
}

void main() {
    float kind = in_params.y;
    vec4 color;
    if (kind == kRect) {
        color = shade_rect();
    } else if (kind == kGlyph) {
        color = shade_glyph();
    } else if (kind == kLayer) {
        color = texture(image, in_uv / vec2(textureSize(image, 0))) * in_color.a;
    } else if (kind == kLayerShadow) {
        color = in_color * texture(image, in_uv / vec2(textureSize(image, 0))).r;
    } else if (kind == kPath) {
        color = fill_color() * texture(image, in_uv / vec2(textureSize(image, 0))).r;
    } else if (kind == kImage) {
        vec4 texel = texture(image, in_uv); // linear (sRGB texture), straight alpha
        color = vec4(texel.rgb * texel.a, texel.a) * in_color.a;
    } else {
        color = shade_box_shadow();
    }
    out_color = color * clip_coverage();
}
