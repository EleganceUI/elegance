---
title: Display list and the UI ubershader
description: "Draw commands, how a frame becomes passes of instanced quads, and what the shader computes."
sidebar:
  order: 3
depends: [renderer/render-thread]
---

## What it's for

Everything a UI draws reduces to a few primitives: rounded rects with fills and borders, glyphs,
shadows, images, paths, clips and groups. This part defines that vocabulary (the display list)
and the pipeline that draws it in very few draw calls. Most frames are one instanced draw per
pass.

## How it works

### Units and color

The display list works in **logical points** and **sRGB with straight alpha**, the way app code
thinks. The planner converts to **pixels**, and the shader works in **linear, premultiplied**
color. Gradients are the exception: their stops are interpolated in sRGB with premultiplied alpha
(as CSS does), then converted to linear.

### Draw commands

A `DisplayList` is `{ clear_color, scale, commands, animating, cull }`. Commands, in painter's
order:

| Command | Meaning |
|---|---|
| `RectCommand` | A rect with per-corner radii, a fill (`ShapeStyle`: color or gradient), and an optional border |
| `GlyphCommand` | A glyph quad, its atlas uv, color, and em size |
| `BoxShadowCommand` | An analytic blurred rounded rect (the cheap shadow for single shapes) |
| `PathCommand` | A filled path (see [GPU vector paths](../../graphics/paths/)) |
| `ImageCommand`, `VideoCommand` | Textured quads |
| `PushClipCommand` / `PopClipCommand` | A rounded-rect clip for everything between |
| `PushLayerCommand` / `PopLayerCommand` | Render everything between offscreen, then composite with opacity, an optional shadow, and an optional path mask |

`animating` is set during painting by anything mid-animation, and tells the loop to schedule
another frame. `cull` is the visible rect while painting inside a scroll view, so elements outside
it skip painting.

Helpers used by elements: `append_faded(list, content, opacity)` fades a group, using an alpha
multiply when `content` is a single shape and a layer otherwise. `transform` moves and scales
commands for transitions.

### Frame planning

`plan_frame(list)` turns a display list into a `FramePlan` with no Vulkan in it, so it can be unit
tested:

1. **Walk the commands with a clip stack.** The effective clip is two things: the **innermost
   rounded clip** at its own rect and radii, and a **scissor**, the intersection of every enclosing
   clip's rect. A plain rect clip only narrows the scissor. A new rounded clip becomes the shape,
   and the enclosing shape's rect joins the scissor.
2. **Cull** instances entirely outside the scissor.
3. **Split layers into passes.** A layer's content goes into its own `PlanPass`. Layers are listed
   innermost first, so every layer is rendered before the pass that composites it. A layer gets
   margin around its content for anti-aliasing and blur.
4. **Emit instances** into one `QuadInstance` array: layers first, in render order, then the main
   pass. Each pass is a list of **segments**, runs of instances that sample the same image (the
   glyph atlas, one layer, one layer's shadow, one path mask, one image, one video).
5. **Collect gradient stops** into one array for the frame. An instance points at its first stop
   and count.

### The instance

`QuadInstance` is ten `vec4`s (160 bytes): rect; shape (radii, or atlas uv, or texel rect); color;
border color; params (border width or distance range, **kind**, paint type, first stop); gradient
geometry; clip rect; clip radii; extra (stop count, shadow sigma); clip scissor.

### The shaders

The vertex shader draws a 4-vertex strip per instance, expanded by 1 px for anti-aliasing (or 3σ
+ 1 for box shadows), and passes everything to the fragment shader as flat varyings. The fragment
shader switches on **kind**:

| Kind | Computes |
|---|---|
| rect | Rounded-box signed distance; coverage `clamp(0.5 − d)`; border by offsetting the distance by the border width; fill from the paint |
| glyph | `median(r, g, b) − 0.5` of the MSDF texel, scaled by the on-screen distance range |
| layer | The layer texture × opacity |
| layer shadow | The blurred R8 alpha × shadow color |
| box shadow | Evan Wallace's closed form: erf along x, a 4-sample integral along y |
| path | Fill × the path's R8 coverage mask |
| image | Texel (sRGB texture, straight alpha), premultiplied × opacity |

Every kind is then multiplied by **clip coverage**: the rounded-box coverage of the innermost clip
times the box coverage of the scissor. That product is why a circle half scrolled out of a
rectangular scroll view keeps its shape and gets a straight cut.

### Layers and blur

A layer is an RGBA8 sRGB image holding premultiplied linear color, pooled per frame slot. Sizes
are rounded up so small changes reuse the same image. A layer with a shadow gets two extra R8
passes, a horizontal then a vertical separable Gaussian of its alpha. The composite draws the
shadow (offset, tinted), then the layer.

### Pipelines

One pipeline layout: set 0 is one combined image sampler, set 1 is the gradient-stop storage
buffer, and a push constant carries the viewport size. Pipelines are cached by target format and
blend mode: "over" (premultiplied source-over) by default, and "destination-in" to mask a layer
with a path.

## Rules

- Offscreen passes only for groups that need them (opacity over several shapes, layer shadows,
  path clips). Single shapes use fast paths.
- One instance buffer and one stop buffer per frame.
- Planning is pure: same display list in, same plan out.

## Edge cases

- **Two overlapping rounded clips**: exact only when the outer one's corners don't cut into the
  inner one; otherwise the outer corners act as its rect.
- **Empty or fully clipped layers** are skipped.
- **Huge layers** are clamped to a maximum texture size (`kMaxLayerSize`); content beyond it is
  cut off. Clamping to the visible part instead would be a cheap improvement.
- **Layer margins**: 1 px for anti-aliasing, or `3σ + 2` px when the layer casts a shadow, with
  σ = half the shadow radius in pixels.
