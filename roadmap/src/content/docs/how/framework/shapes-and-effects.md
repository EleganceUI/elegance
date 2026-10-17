---
title: Shapes, decorations, and effects
description: "Shape views and their fills, background/overlay/border, and how clips, opacity, shadows and offsets are painted."
sidebar:
  order: 4
depends: [framework/layout]
---

## What it's for

Shapes are the simplest real views: they fill the space offered and draw one primitive.
Decorations put a view behind or in front of another. Effects change how a view's content is
drawn (clipped, faded, shadowed, moved) without changing layout. Together they cover most visual
styling.

## How it works

### Shapes

`Rectangle`, `RoundedRectangle(r)`, `Capsule`, `Circle` and `Ellipse` are `ShapeNode`s carrying
a `ShapeSpec`: the kind and radii, an optional **fill** (`Prop<ShapeStyle>`), and an optional
**stroke** (color, width, inside or centered). They take the proposal as their size (10×10 when
an axis is unspecified).

- `.fill(style)` binds the fill reactively and animatably.
- `.stroke(color, width)` strokes centered on the outline; `.stroke_border` strokes inside it.
- A shape with **neither** a fill nor a stroke fills with the environment's **foreground**, so
  `Circle().foreground_style(c)` works like SwiftUI.
- Analytic shapes paint as one `RectCommand`: a rect with radii (a capsule's radius is half its
  short side). A stroke is the border of that command. Paths and dashed strokes go through the
  path pipeline (see [GPU vector paths](../../graphics/paths/)).

Hit testing follows the outline. A stroke-only shape is hit on its line.

### Decorations

`DecorationElement` holds the content and a secondary element. Layout sees **only the content**:
the secondary view is proposed the content's size and aligned inside its frame. Painting draws
the secondary before the content (`behind`: background) or after (`in_front`: overlay, border).
`border(color, width)` is an overlay of a stroked rectangle.

Because decorations size to the content they wrap, order matters: `.padding().background()`
paints behind the padded area, and `.background().padding()` only behind the content.

### Effects

An `EffectModifier` fills an `Effects` struct: clip to bounds, clip shape, opacity, shadow,
offset x/y, z-index, content shape, and whether hit testing is allowed. `EffectElement` paints
its content in this order:

1. **Paint the content into a temporary list**, so the element can see what it consists of.
2. **Clip**:
   - `clipped()` → a rect clip.
   - `clip_shape(RoundedRectangle | Capsule | Circle)` → a rounded clip; the shader does it
     analytically, with no extra pass.
   - `clip_shape(path)` → a layer whose path mask keeps only what the path covers.
3. **Offset**: move every command by the current offset. Offsets are animatable, and an
   animating offset marks the list as animating.
4. **Opacity and shadow**, in the cheapest way that's correct:
   - fully opaque, no shadow → append the commands as they are;
   - fully transparent → draw nothing;
   - **a single filled shape** → multiply its alpha, and draw its shadow as an analytic
     `BoxShadowCommand`;
   - **anything else** → wrap the content in a layer with that opacity and shadow, so overlapping
     parts fade as one group and the shadow follows the actual alpha.

The fast path matters: most `.opacity()` and `.shadow()` calls are on a single shape, and
sending those to an offscreen layer would make ordinary screens expensive.

### Canvas

`Canvas(draw)` calls `draw(GraphicsContext&)` while painting, with the element's frame. The
context appends commands: rects, rounded rects, text, and later paths. A canvas isn't cullable,
because it may draw anywhere.

## Rules

- Effects never change layout. Offsets move drawing **and** hit testing, but not the frame.
- Clip first, then fade and shadow the clipped result.
- Use an offscreen layer only when a fast path can't give the same pixels.

## Edge cases

- **Opacity animating to 0** keeps the element in the tree, and painting skips it. Removal is the
  job of `If`/`ForEach` transitions.
- **A shadow on a group** blurs the group's alpha, so a text label casts a text-shaped shadow.
- Known gap: Button's and tap's "released inside" checks use the un-offset frame.
