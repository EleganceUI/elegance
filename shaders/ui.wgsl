// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// One instance per UI primitive, drawn as a 4-vertex triangle strip, in painter's order.
// Units are pixels of the current render target, y down.

// The glyph atlas (MTSDF), a layer's texture, a path mask, an image, or a video frame.
@group(0) @binding(0) var image: texture_2d<f32>;
@group(0) @binding(1) var image_sampler: sampler;

// Gradient stops, two entries each: sRGB color with straight alpha, then (location, 0, 0, 0).
// Color matrices, six entries each: the rows for red, green, blue and alpha (coefficients of r,
// g, b, a in sRGB with straight alpha), then the offsets, then padding.
@group(1) @binding(0) var<storage, read> stops: array<vec4f>;

// A second image: a snapshot of what the pass drew before (materials, blend modes), or a layer's
// content beside its inner shadow. Unused otherwise (a transparent texel).
@group(2) @binding(0) var second: texture_2d<f32>;
@group(2) @binding(1) var second_sampler: sampler;

struct Immediates {
    viewport: vec2f, // pixels
}
var<immediate> pc: Immediates;

struct Instance {
    @location(0) rect: vec4f,         // x, y, width, height
    @location(1) shape: vec4f,        // radii | atlas uv | layer texel rect
    @location(2) color: vec4f,        // linear, premultiplied
    @location(3) border_color: vec4f, // linear, premultiplied | per-kind (see below)
    @location(4) params: vec4f,       // x: border width | distance range; y: kind; z: paint; w: first stop
    @location(5) gradient: vec4f,     // the paint's geometry | layers: their 2x2 transform
    @location(6) clip_rect: vec4f,    // x, y, width, height
    @location(7) clip_radii: vec4f,
    @location(8) extra: vec4f,        // x: stop count; y: shadow sigma; zw: per-kind
    @location(9) clip_scissor: vec4f, // x, y, width, height: the enclosing clips' rects
}

struct Varyings {
    @builtin(position) position: vec4f,
    @location(0) local: vec2f, // position relative to the rect's center
    @location(1) uv: vec2f,
    @location(2) @interpolate(flat) half_size: vec2f,
    @location(3) @interpolate(flat) shape: vec4f,
    @location(4) @interpolate(flat) color: vec4f,
    @location(5) @interpolate(flat) border_color: vec4f,
    @location(6) @interpolate(flat) params: vec4f,
    @location(7) @interpolate(flat) gradient: vec4f,
    @location(8) @interpolate(flat) clip_rect: vec4f,
    @location(9) @interpolate(flat) clip_radii: vec4f,
    @location(10) @interpolate(flat) extra: vec4f,
    @location(11) @interpolate(flat) clip_scissor: vec4f,
}

// Primitive kinds (params.y).
const kRect = 0.0;        // rounded rect: shape = radii, fill (params.z paint), border
const kGlyph = 1.0;       // MSDF glyph: uv = atlas uv, params.x = distance range in px
// A layer's texture: uv = texels, color.a = opacity, params.x = color matrix index + 1 (0: none),
// params.z = blend mode (0: normal), border_color.xy = the backdrop snapshot's origin (blending).
const kLayer = 2.0;
const kLayerShadow = 3.0; // a layer's blurred alpha (R8): uv = texels, color = shadow
const kBoxShadow = 4.0;   // analytic blurred rounded rect: extra.y = sigma
const kPath = 5.0;        // a path's coverage mask (R8): uv = texels; fill as for rects
const kImage = 6.0;       // a raster image (sRGB, straight alpha): uv normalized, color.a = opacity
// Analytic shadow inside a rounded rect: shape = radii, extra.y = sigma, border_color.xy = offset,
// params.x = spread.
const kInnerShadow = 7.0;
// A layer's inner shadow: its blurred inverse alpha (R8, group 0) where its content (group 2) is;
// uv = texels, color = shadow, border_color.xy = offset.
const kLayerInnerShadow = 8.0;
// Layer kinds (kLayer, kLayerShadow, kLayerInnerShadow) turn and scale: gradient = (a, b, c, d)
// with x' = a x + c y, y' = b x + d y, about extra.zw.

// Paints (params.z).
const kSolid = 0.0;
const kLinear = 1.0;     // gradient = start.xy, end.xy
const kRadial = 2.0;     // gradient = center.xy, r0, r1
const kElliptical = 3.0; // gradient = center.xy, bounds size; extra.zw = radius fractions
const kAngular = 4.0;    // gradient = center.xy, start and end angle (radians)
// A material: the backdrop snapshot (group 2), blurred, at (p - gradient.xy) / gradient.z;
// params.w = color matrix index + 1; gradient.w = noise.
const kBackdrop = 5.0;

const kTau = 6.283185307179586;

fn is_layer_kind(kind: f32) -> bool {
    return kind == kLayer || kind == kLayerShadow || kind == kLayerInnerShadow;
}

@vertex
fn vs_main(@builtin(vertex_index) vertex: u32, in: Instance) -> Varyings {
    let corner = vec2f(f32(vertex & 1u), f32(vertex >> 1u)); // (0,0) (1,0) (0,1) (1,1)

    // Rects need a pixel outside for anti-aliasing; box shadows extend three sigma further.
    var margin = 0.0;
    if (in.params.y == kRect || in.params.y == kInnerShadow) {
        margin = 1.0;
    } else if (in.params.y == kBoxShadow) {
        margin = 3.0 * in.extra.y + 1.0;
    }

    let half_size = in.rect.zw * 0.5;
    let center = in.rect.xy + half_size;
    let local = (corner * 2.0 - 1.0) * (half_size + margin);
    var position = center + local;
    if (is_layer_kind(in.params.y)) {
        var m = in.gradient;
        if (all(m == vec4f(0.0))) {
            m = vec4f(1.0, 0.0, 0.0, 1.0);
        }
        let pivot = in.extra.zw;
        position = pivot + mat2x2f(m.xy, m.zw) * (position - pivot);
    }

    // WebGPU clip space has y pointing up; pixel coordinates point down.
    let ndc = position / pc.viewport * 2.0 - 1.0;

    var out: Varyings;
    out.position = vec4f(ndc.x, -ndc.y, 0.0, 1.0);
    out.local = local;
    out.uv = mix(in.shape.xy, in.shape.zw, corner);
    out.half_size = half_size;
    out.shape = in.shape;
    out.color = in.color;
    out.border_color = in.border_color;
    out.params = in.params;
    out.gradient = in.gradient;
    out.clip_rect = in.clip_rect;
    out.clip_radii = in.clip_radii;
    out.extra = in.extra;
    out.clip_scissor = in.clip_scissor;
    return out;
}

// Signed distance in pixels to a rounded box centered at the origin, negative inside.
// radii = (top-left, top-right, bottom-right, bottom-left); y points down.
fn rounded_box_distance(p: vec2f, half_size: vec2f, radii: vec4f) -> f32 {
    var r: f32;
    if (p.x > 0.0) {
        r = select(radii.y, radii.z, p.y > 0.0);
    } else {
        r = select(radii.x, radii.w, p.y > 0.0);
    }
    let q = abs(p) - half_size + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, vec2f(0.0))) - r;
}

fn median(a: f32, b: f32, c: f32) -> f32 {
    return max(min(a, b), min(max(a, b), c));
}

fn srgb_to_linear(c: vec3f) -> vec3f {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3f(2.4)), step(vec3f(0.04045), c));
}

fn linear_to_srgb(c: vec3f) -> vec3f {
    let v = max(c, vec3f(0.0));
    return mix(v * 12.92, 1.055 * pow(v, vec3f(1.0 / 2.4)) - 0.055, step(vec3f(0.0031308), v));
}

// Premultiplied sRGB -> premultiplied linear.
fn to_linear(premultiplied: vec4f) -> vec4f {
    let a = premultiplied.a;
    let straight = select(vec3f(0.0), premultiplied.rgb / a, a > 0.0);
    return vec4f(srgb_to_linear(straight) * a, a);
}

// Premultiplied linear -> straight sRGB, and back.
fn to_straight_srgb(c: vec4f) -> vec4f {
    let straight = select(vec3f(0.0), c.rgb / c.a, c.a > 0.0);
    return vec4f(linear_to_srgb(straight), c.a);
}

fn from_straight_srgb(c: vec4f) -> vec4f {
    let s = clamp(c, vec4f(0.0), vec4f(1.0));
    return vec4f(srgb_to_linear(s.rgb) * s.a, s.a);
}

// A color matrix from the stops buffer (index = its first entry), applied to a premultiplied
// linear color in sRGB with straight alpha, as CSS filters are.
fn apply_matrix(c: vec4f, index: i32) -> vec4f {
    let v = to_straight_srgb(c);
    let out = vec4f(dot(stops[index], v), dot(stops[index + 1], v), dot(stops[index + 2], v),
                    dot(stops[index + 3], v)) + stops[index + 4];
    return from_straight_srgb(out);
}

fn stop_color(index: i32) -> vec4f {
    let c = stops[2 * index];
    return vec4f(c.rgb * c.a, c.a);
}

// Interpolates stops in sRGB with premultiplied alpha, as CSS does.
fn gradient_color(in: Varyings, position: f32) -> vec4f {
    let first = i32(in.params.w);
    let count = i32(in.extra.x);
    let t = clamp(position, 0.0, 1.0);
    var previous = stop_color(first);
    var previous_location = stops[2 * first + 1].x;
    if (t <= previous_location) {
        return to_linear(previous);
    }
    for (var i = 1; i < count; i++) {
        let color = stop_color(first + i);
        let location = stops[2 * (first + i) + 1].x;
        if (t <= location) {
            let f = (t - previous_location) / max(location - previous_location, 1e-6);
            return to_linear(mix(previous, color, f));
        }
        previous = color;
        previous_location = location;
    }
    return to_linear(previous);
}

// A well-mixed hash of a pixel (PCG), 0...1.
fn pcg(v: u32) -> u32 {
    let state = v * 747796405u + 2891336453u;
    let word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

fn noise(p: vec2f) -> f32 {
    let q = vec2u(max(p, vec2f(0.0)));
    return f32(pcg(q.x + pcg(q.y))) / 4294967295.0;
}

// What a material shows: the blurred backdrop, its colors boosted, with grain.
fn backdrop_color(in: Varyings, p: vec2f) -> vec4f {
    let uv = (p - in.gradient.xy) / in.gradient.z / vec2f(textureDimensions(second));
    var color = textureSampleLevel(second, second_sampler, uv, 0.0);
    if (in.params.w > 0.0) {
        color = apply_matrix(color, i32(in.params.w) - 1);
    }
    if (in.gradient.w > 0.0) {
        var s = to_straight_srgb(color);
        s = vec4f(s.rgb + (noise(p) - 0.5) * in.gradient.w, s.a);
        color = from_straight_srgb(s);
    }
    return color;
}

fn fill_color(in: Varyings) -> vec4f {
    let p = in.position.xy;
    let paint = in.params.z;
    if (paint == kLinear) {
        let start = in.gradient.xy;
        let direction = in.gradient.zw - start;
        return gradient_color(in, dot(p - start, direction) / max(dot(direction, direction), 1e-6));
    }
    if (paint == kRadial) {
        let distance = length(p - in.gradient.xy);
        return gradient_color(in, (distance - in.gradient.z) / max(in.gradient.w - in.gradient.z, 1e-6));
    }
    if (paint == kElliptical) {
        let distance = length((p - in.gradient.xy) / max(in.gradient.zw, vec2f(1e-6)));
        return gradient_color(in, (distance - in.extra.z) / max(in.extra.w - in.extra.z, 1e-6));
    }
    if (paint == kAngular) {
        let v = p - in.gradient.xy;
        var angle = atan2(v.y, v.x) - in.gradient.z; // y down: clockwise
        angle = angle - floor(angle / kTau) * kTau;  // 0...2pi past the start
        return gradient_color(in, angle / max(in.gradient.w - in.gradient.z, 1e-6));
    }
    if (paint == kBackdrop) {
        return backdrop_color(in, p);
    }
    return in.color;
}

fn shade_rect(in: Varyings) -> vec4f {
    let d = rounded_box_distance(in.local, in.half_size, in.shape);
    // Analytic anti-aliasing: coverage ramps over one pixel centered on the edge.
    let coverage = clamp(0.5 - d, 0.0, 1.0);

    var color = fill_color(in);
    let border_width = in.params.x;
    if (border_width > 0.0) {
        // Offsetting the distance gives the inner edge, with radii shrunk by the border width.
        let inside_border = clamp(0.5 - (d + border_width), 0.0, 1.0);
        color = mix(in.border_color, color, inside_border);
    }
    return color * coverage;
}

// Textures other than images have a single level, so sampling level 0 is exact; it also keeps
// sampling legal outside uniform control flow (the branches below depend on per-instance data).
fn sample_texels(texels: vec2f) -> vec4f {
    return textureSampleLevel(image, image_sampler, texels / vec2f(textureDimensions(image)), 0.0);
}

fn shade_glyph(in: Varyings) -> vec4f {
    let field = textureSampleLevel(image, image_sampler, in.uv, 0.0);
    // 0.5 is the glyph edge; scale the field's units to screen pixels for a 1px AA ramp.
    let distance = median(field.r, field.g, field.b) - 0.5;
    let screen_range = max(in.params.x, 1.0);
    let coverage = clamp(distance * screen_range + 0.5, 0.0, 1.0);
    return in.color * coverage;
}

// --- Analytic rounded-rect shadow (Evan Wallace, "Fast rounded rectangle shadows") ---

fn erf2(v: vec2f) -> vec2f {
    let s = sign(v);
    let a = abs(v);
    var x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
    x *= x;
    return s - s / (x * x);
}

fn gaussian(x: f32, sigma: f32) -> f32 {
    let pi = 3.141592653589793;
    return exp(-(x * x) / (2.0 * sigma * sigma)) / (sqrt(2.0 * pi) * sigma);
}

fn rounded_box_shadow_x(x: f32, y: f32, sigma: f32, corner: f32, half_size: vec2f) -> f32 {
    let delta = min(half_size.y - corner - abs(y), 0.0);
    let curved = half_size.x - corner + sqrt(max(0.0, corner * corner - delta * delta));
    let integral = 0.5 + 0.5 * erf2((x + vec2f(-curved, curved)) * (sqrt(0.5) / sigma));
    return integral.y - integral.x;
}

fn rounded_box_shadow(point: vec2f, half_size: vec2f, sigma: f32, corner: f32) -> f32 {
    let low = point.y - half_size.y;
    let high = point.y + half_size.y;
    let start = clamp(-3.0 * sigma, low, high);
    let end = clamp(3.0 * sigma, low, high);
    let step_size = (end - start) / 4.0;
    var y = start + step_size * 0.5;
    var value = 0.0;
    for (var i = 0; i < 4; i++) {
        value += rounded_box_shadow_x(point.x, point.y - y, sigma, corner, half_size) * gaussian(y, sigma) * step_size;
        y += step_size;
    }
    return value;
}

fn max_radius(radii: vec4f) -> f32 {
    return max(max(radii.x, radii.y), max(radii.z, radii.w));
}

fn shade_box_shadow(in: Varyings) -> vec4f {
    let sigma = max(in.extra.y, 0.5);
    let corner = min(max_radius(in.shape), min(in.half_size.x, in.half_size.y));
    return in.color * rounded_box_shadow(in.local, in.half_size, sigma, corner);
}

// Inside the shape, the part a blurred copy of it (moved, shrunk by the spread) doesn't light.
fn shade_inner_shadow(in: Varyings) -> vec4f {
    let inside = clamp(0.5 - rounded_box_distance(in.local, in.half_size, in.shape), 0.0, 1.0);
    let sigma = max(in.extra.y, 0.5);
    let spread = in.params.x;
    let half_size = max(in.half_size - spread, vec2f(0.0));
    let corner = clamp(max_radius(in.shape) - spread, 0.0, min(half_size.x, half_size.y));
    let lit = rounded_box_shadow(in.local - in.border_color.xy, half_size, sigma, corner);
    return in.color * (1.0 - clamp(lit, 0.0, 1.0)) * inside;
}

// --- Blend modes (W3C Compositing and Blending Level 1), on straight sRGB colors ---

fn multiply(b: vec3f, s: vec3f) -> vec3f { return b * s; }
fn screen(b: vec3f, s: vec3f) -> vec3f { return b + s - b * s; }

fn hard_light(b: vec3f, s: vec3f) -> vec3f {
    return select(screen(b, 2.0 * s - 1.0), multiply(b, 2.0 * s), s <= vec3f(0.5));
}

fn color_dodge(b: vec3f, s: vec3f) -> vec3f {
    let dodged = min(vec3f(1.0), b / max(1.0 - s, vec3f(1e-6)));
    return select(select(dodged, vec3f(1.0), s >= vec3f(1.0)), vec3f(0.0), b <= vec3f(0.0));
}

fn color_burn(b: vec3f, s: vec3f) -> vec3f {
    let burned = 1.0 - min(vec3f(1.0), (1.0 - b) / max(s, vec3f(1e-6)));
    return select(select(burned, vec3f(0.0), s <= vec3f(0.0)), vec3f(1.0), b >= vec3f(1.0));
}

fn soft_light(b: vec3f, s: vec3f) -> vec3f {
    let d = select(sqrt(b), ((16.0 * b - 12.0) * b + 4.0) * b, b <= vec3f(0.25));
    return select(b + (2.0 * s - 1.0) * (d - b), b - (1.0 - 2.0 * s) * b * (1.0 - b), s <= vec3f(0.5));
}

fn lum(c: vec3f) -> f32 {
    return dot(c, vec3f(0.3, 0.59, 0.11));
}

fn clip_color(c: vec3f) -> vec3f {
    let l = lum(c);
    let n = min(min(c.r, c.g), c.b);
    let x = max(max(c.r, c.g), c.b);
    var out = c;
    if (n < 0.0) {
        out = l + (out - l) * l / max(l - n, 1e-6);
    }
    if (x > 1.0) {
        out = l + (out - l) * (1.0 - l) / max(x - l, 1e-6);
    }
    return out;
}

fn set_lum(c: vec3f, l: f32) -> vec3f {
    return clip_color(c + (l - lum(c)));
}

fn sat(c: vec3f) -> f32 {
    return max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b);
}

fn set_sat(c: vec3f, s: f32) -> vec3f {
    let n = min(min(c.r, c.g), c.b);
    let range = sat(c);
    return select(vec3f(0.0), (c - n) * s / range, range > 0.0);
}

// Modes in elg::BlendMode's order.
fn blend_colors(b: vec3f, s: vec3f, mode: i32) -> vec3f {
    switch (mode) {
        case 1: { return multiply(b, s); }
        case 2: { return min(b, s); }
        case 3: { return color_burn(b, s); }
        case 4: { return max(b + s - 1.0, vec3f(0.0)); }       // plus darker
        case 5: { return screen(b, s); }
        case 6: { return max(b, s); }
        case 7: { return color_dodge(b, s); }
        case 8: { return min(b + s, vec3f(1.0)); }             // plus lighter
        case 9: { return hard_light(s, b); }                   // overlay
        case 10: { return soft_light(b, s); }
        case 11: { return hard_light(b, s); }
        case 12: { return abs(b - s); }                        // difference
        case 13: { return b + s - 2.0 * b * s; }               // exclusion
        case 14: { return set_lum(set_sat(s, sat(b)), lum(b)); } // hue
        case 15: { return set_lum(set_sat(b, sat(s)), lum(b)); } // saturation
        case 16: { return set_lum(s, lum(b)); }                // color
        case 17: { return set_lum(b, lum(s)); }                // luminosity
        default: { return s; }
    }
}

// The source (premultiplied linear) blended with the backdrop snapshot under it, as a
// premultiplied color for "over": over adds the backdrop's (1 - alpha) share.
fn blend(source: vec4f, in: Varyings) -> vec4f {
    let a_s = source.a;
    if (a_s <= 0.0) {
        return vec4f(0.0);
    }
    let texel = vec2i(floor(in.position.xy - in.border_color.xy));
    let backdrop = to_straight_srgb(textureLoad(second, texel, 0));
    let s = to_straight_srgb(source).rgb;
    let mixed = (1.0 - backdrop.a) * s + backdrop.a * blend_colors(backdrop.rgb, s, i32(in.params.z));
    return vec4f(srgb_to_linear(clamp(mixed, vec3f(0.0), vec3f(1.0))) * a_s, a_s);
}

fn shade_layer(in: Varyings) -> vec4f {
    var color = sample_texels(in.uv);
    if (in.params.x > 0.0) {
        color = apply_matrix(color, i32(in.params.x) - 1);
    }
    color *= in.color.a;
    if (in.params.z > 0.0) {
        color = blend(color, in);
    }
    return color;
}

fn shade_layer_inner_shadow(in: Varyings) -> vec4f {
    let shadow = sample_texels(in.uv - in.border_color.xy).r;
    let content = textureSampleLevel(second, second_sampler, in.uv / vec2f(textureDimensions(second)), 0.0).a;
    return in.color * shadow * content;
}

// --- Clipping ---

// The innermost rounded clip, at its own size, times the enclosing clips' rect: a circle half
// scrolled out of view stays a circle with a straight cut.
fn clip_coverage(in: Varyings) -> f32 {
    let p = in.position.xy;
    let half_size = in.clip_rect.zw * 0.5;
    let center = in.clip_rect.xy + half_size;
    let shape = clamp(0.5 - rounded_box_distance(p - center, half_size, in.clip_radii), 0.0, 1.0);
    let scissor_half = in.clip_scissor.zw * 0.5;
    let scissor_center = in.clip_scissor.xy + scissor_half;
    let scissor = clamp(0.5 - rounded_box_distance(p - scissor_center, scissor_half, vec4f(0.0)), 0.0, 1.0);
    return shape * scissor;
}

@fragment
fn fs_main(in: Varyings) -> @location(0) vec4f {
    // Images are mipmapped: their level of detail comes from derivatives, which must be taken in
    // uniform control flow, before branching on the kind.
    let uv_dx = dpdx(in.uv);
    let uv_dy = dpdy(in.uv);

    let kind = in.params.y;
    var color: vec4f;
    if (kind == kRect) {
        color = shade_rect(in);
    } else if (kind == kGlyph) {
        color = shade_glyph(in);
    } else if (kind == kLayer) {
        color = shade_layer(in);
    } else if (kind == kLayerShadow) {
        color = in.color * sample_texels(in.uv).r;
    } else if (kind == kPath) {
        color = fill_color(in) * sample_texels(in.uv).r;
    } else if (kind == kImage) {
        let texel = textureSampleGrad(image, image_sampler, in.uv, uv_dx, uv_dy); // linear (sRGB texture), straight alpha
        color = vec4f(texel.rgb * texel.a, texel.a) * in.color.a;
    } else if (kind == kInnerShadow) {
        color = shade_inner_shadow(in);
    } else if (kind == kLayerInnerShadow) {
        color = shade_layer_inner_shadow(in);
    } else {
        color = shade_box_shadow(in);
    }
    return color * clip_coverage(in);
}
