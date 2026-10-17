---
title: Shapes, decorations, and effects
description: Shape views, fills and strokes, background/overlay/border, clips, opacity, shadows, and Canvas.
sidebar:
  order: 4
---

This page gets the first real views on screen: shapes, decorations on any view, and the effect
modifiers that change how content is drawn without changing layout.

:::note[Depends on]
[Layout](../layout/).
:::

## Read first

- `design.md` §9.4 (Catalog; clipping and masking cost model), §9.5 (Shapes), M1
- `include/elg/shapes.h`, `include/elg/modifiers/decoration.h`, `include/elg/modifiers/effects.h`, `include/elg/graphics_context.h`
- `src/view/shapes.cpp`, `src/graphics_context.cpp`, `ShapeElement` / `EffectElement` / `DecorationElement` / `CanvasElement` in `src/view/element.*`

## Tasks

### Shapes
- [ ] `Rectangle`, `RoundedRectangle(r)`, `Capsule`, `Circle`, and `Ellipse`. They fill the proposal; the ideal size is 10×10
- [ ] `Shape::fill(Prop<ShapeStyle>)`, `stroke(style, width)`, `stroke_border`, `inset(by)`
- [ ] A shape with neither a fill nor a stroke uses the environment's foreground
- [ ] `ShapeSpec` / `ShapeElement`; resolve the fill reactively, animatable

### Decorations
- [ ] `DecorationElement`: the secondary view is sized to the content, drawn behind or in front, and doesn't affect layout
- [ ] `background(View | Color | Gradient | Shape)`, `overlay(view, alignment)`, `border(color, width)`

### Effects
- [ ] `Effects { clip_to_bounds, clip_shape, opacity, shadow, offset_x/y, z_index, content_shape, allows_hit_testing }`
- [ ] `clipped()`: a rect clip, effectively free
- [ ] `clip_shape(RoundedRectangle | Capsule | Circle)` and `corner_radius(r)`: an analytic SDF clip, no offscreen pass
- [ ] `opacity(Prop<float>)`: alpha multiply for a single shape, an offscreen layer otherwise
- [ ] `shadow({.radius, .x, .y, .color})`: an analytic box shadow for a single shape; a blurred layer alpha otherwise
- [ ] Order: clip first, then opacity and shadow of the clipped result
- [ ] `offset(x, y)`: reactive and animatable; moves drawing and hit testing only

### Canvas
- [ ] `Canvas([](GraphicsContext& g) { ... })`: immediate-mode drawing into the element's rect (fill rects, rounded rects, text, and later paths)
- [ ] Not cullable (it can draw anywhere)

## Done when

- [ ] The counter example from `design.md` §9.1 renders: gradient background, rounded clip, shadow
- [ ] `.opacity(0.5)` on a `VStack` of overlapping shapes fades as one group (no double-darkened overlap)
- [ ] An offset view is hit where it's drawn, not where it's laid out

## Gotchas

- Single-shape fast paths matter. Most `.opacity()` and `.shadow()` calls are on one shape;
  sending them to an offscreen layer would make ordinary UIs expensive.
- Known gap: Button and tap "released inside" checks still use the un-offset frame.
- Not built yet: `mask(View)`, `blur`, `brightness`, `saturation`, `blend_mode`,
  `rotation_effect`, `scale_effect` as a modifier, `background_material`, and modifier fusion.
