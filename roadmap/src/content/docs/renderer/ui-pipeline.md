---
title: Display list and the UI ubershader
description: Draw commands, frame planning, SDF rects, clipping, layers, shadows, and gradients in one pipeline.
sidebar:
  order: 3
---

Almost everything Elegance draws is an instanced quad in one pipeline, in painter's order. The
fragment shader switches on a "kind" per instance. Offscreen passes happen only for groups,
blur, and masks.

:::note[Depends on]
[Render thread and frame mailbox](../render-thread/). Geometry and color types (`Rect`, `Point`,
`Size`, `CornerRadii`, `Color`, `ShapeStyle`): write `include/elg/geometry.h`, `color.h` and
`gradient.h` now.
:::

## Read first

- `design.md` §5 (Primitives, Batching), M1 effects notes, "exact clipping" in M5
- `src/render/display_list.*`, `src/render/frame_plan.*`, `src/render/color_space.h`
- `src/gpu/quad_renderer.*`, `src/gpu/blur_renderer.*`, `shaders/ui.vert`, `shaders/ui.frag`, `shaders/blur.*`

## Tasks

### Value types
- [ ] `Point`, `Size`, `Rect` (with `inset`, `max_x`...), `ProposedSize` (each axis `optional<float>`), `CornerRadii`, `Insets`, `infinity`
- [ ] `Color` in sRGB with straight alpha: `hex`, `opacity`, `darker`, named colors
- [ ] `LinearGradient` and `RadialGradient` with any number of stops and `UnitPoint`s; `ShapeStyle` = a color or a gradient

### Display list
- [ ] A `DrawCommand` variant: `RectCommand` (rect, radii, fill, border color/width), `GlyphCommand`, `BoxShadowCommand`, `PushClipCommand` / `PopClipCommand`, `PushLayerCommand` (bounds, opacity, optional shadow) / `PopLayerCommand`
- [ ] `DisplayList { clear_color, scale, commands, animating, cull }`. Logical points and sRGB. It holds copies, never references into app memory
- [ ] Helpers: `draw_bounds`, `union_of`, `transform` (move and scale commands for transitions), `append_faded` (a single shape gets an alpha multiply; anything else gets a layer)

### Frame planning (no Vulkan here, so it's unit-testable)
- [ ] `plan_frame(list)` converts points to pixels
- [ ] Resolve the clip stack per instance, and cull instances that are fully clipped
- [ ] Split layers into their own passes, innermost first (every layer comes after the layers it contains)
- [ ] Collect gradient stops into one array (two vec4s per stop: sRGB color, then location)
- [ ] Group instances into segments by the image they sample (atlas, a layer, a layer's shadow)

### The UI pipeline
- [ ] `QuadInstance` (160 bytes): rect, shape (radii | atlas uv | texel rect), color, border color, params (border width or distance range, kind, paint, first stop), gradient, clip rect, clip radii, extra (stop count, shadow sigma), clip scissor
- [ ] Vertex shader: a 4-vertex strip per instance, expanded by 1 px for AA (3σ+1 for box shadows); Vulkan clip space y down
- [ ] Fragment, **rect**: rounded-box SDF with per-corner radii; coverage `clamp(0.5 - d)`; border by offsetting the distance
- [ ] Fragment, **paint**: solid, linear, radial. Interpolate stops in sRGB with premultiplied alpha (as CSS does), output linear
- [ ] Fragment, **box shadow**: Evan Wallace's analytic blurred rounded rect (erf approximation, 4-sample integral)
- [ ] Fragment, **clip**: the innermost rounded clip at its own size × the scissor of every enclosing clip's rect
- [ ] Pipelines cached per target format and blend mode ("over" by default, "destination-in" for masks later)
- [ ] Set 0: one combined image sampler; set 1: the gradient-stop storage buffer

### Layers, opacity groups, and shadows
- [ ] Layers: RGBA8 sRGB offscreen images, pooled per frame slot and reused after its fence
- [ ] Composite a layer with its opacity (`kind = layer`)
- [ ] Layer shadow: two-pass separable Gaussian of the layer's alpha into R8 targets, then composite tinted (`kind = layer_shadow`)

## Done when

- [ ] A test scene draws rounded rects with borders, both gradients, a box shadow, nested clips, and a faded group, in about 5 draw calls
- [ ] A circle half scrolled out of a rectangular clip stays a circle with a straight cut (not a lens)
- [ ] `plan_frame` has unit tests for clip culling and layer ordering
- [ ] No validation errors with several layers in one frame

## Gotchas

- **Clip intersection**: intersecting a rounded clip with a plain clip and keeping the rounded
  radii produces a lens shape. Carry the innermost rounded clip at its own size *and* a separate
  scissor rect, then multiply the two coverages. That is why `QuadInstance` is 160 bytes.
- Two rounded clips that overlap near the outer clip's corners are still approximate: the outer
  one acts as its rect.
- The CPU side is sRGB with straight alpha; the shader input is linear premultiplied. Pick one
  conversion point and stick to it.
- Fusion (merging `.padding().background().clip_shape().shadow()` into one node and one quad) and
  pixel snapping of layout results are still open.
