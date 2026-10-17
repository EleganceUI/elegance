---
title: GPU vector paths
description: "Path geometry, flattening and stroking on the CPU, stencil-then-cover masks on the GPU, mask caching, and path clips."
sidebar:
  order: 1
depends: [renderer/ui-pipeline, framework/shapes-and-effects]
---

## What it's for

Anything that isn't a rounded rect needs real vector paths: custom shapes, SVG data, icons with
strokes, dashed borders, COLRv1 emoji, and clipping to arbitrary shapes. Paths are rasterized on
the GPU so they stay sharp at any scale, and their masks are cached so moving them costs nothing.

## How it works

### Geometry (CPU)

A `Path` is a list of subpaths of move, line, quadratic, cubic and close commands, built through
a builder or parsed from SVG:

- **SVG path data** (`Path::svg(d)`) supports every command, relative and absolute, implicit
  repeats, and arc flags packed without separators (`a1 1 0 1110 10`). Errors report the
  character position.
- **Arcs** convert from SVG's endpoint form to center form (SVG 1.1 appendix F.6), scaling radii
  that are too small to reach, then into cubics of at most a quarter turn each.
- Helpers build rects, rounded rects, ellipses and circles (quarter circles as cubics with the
  standard handle length).

**Flattening** turns curves into line segments within a tolerance. The segment count per curve
comes from **Wang's formula** (curvature bound × degree), so flat curves get few segments and
tight ones many. Repeated points are dropped, and every subpath counts as closed for filling, as in
SVG. Tolerance is a quarter of a device pixel (`0.25 / scale` in points), so higher DPI and zoom never
show facets.

`contains(point, rule)` hit-tests the flattened polygons by winding number (non-zero) or parity
(even-odd). `bounds()` and `transformed()` do what they say.

### Stroking

`stroked(style)` turns a stroke into a fill. The stroke becomes a **union of simple pieces**,
all wound the same way:

- one quad per segment (the segment offset both ways by half the width, along its left-hand normal),
- one join piece at every corner, on the outer side: miter (falling back to bevel past the miter
  limit), round (a fan), or bevel (a triangle),
- caps at open ends: butt (nothing), square (a half-width extension), round (a half disc).

Filling the pieces with the **non-zero** rule draws their union. Overlaps and self-intersections
need no special handling, which is what makes this stroker simple. Dashes first split each
polyline into "on" stretches of the pattern, starting `dash_phase` in. A zero-length dash becomes
a dot that round and square caps draw and butt caps don't.

### Rendering (GPU): stencil-then-cover

A `PathCommand` carries **shared, flattened polygons in local points** plus a `scale` and
`offset` that place them. Moving or scaling a path (scrolling, transitions) changes only those two
numbers; the polygons are never re-flattened.

For each path the planner produces a **coverage mask** job:

1. **Fan triangles**: for each polygon, triangles from its first point to each edge. Overlapping
   fan triangles cancel or add exactly as the polygon winds, so no triangulation is needed.
2. **Stencil**: draw the fans into a **multisampled stencil** (up to 8×, whatever both attachments
   support). For non-zero, increment-wrap for front-facing triangles and decrement-wrap for
   back-facing; for even-odd, invert.
3. **Cover**: draw a covering triangle that writes full coverage where the stencil is non-zero
   (or odd).
4. **Resolve** the multisampled color to an **R8 mask**: the average of the samples is the
   anti-aliased coverage.

The UI pass then draws one quad of kind `path`, whose color or gradient is multiplied by the mask.

The stencil format is the first the GPU supports of S8, D24S8 and D32S8 (AMD lacks D24S8).

### Mask caching

Masks are kept per window, keyed by a hash of: the polygons, the scale, the **sub-pixel** part of
the offset snapped to ¼ px, the size, and the fill rule. The integer part of the position isn't in
the key, because the mask moves with its quad. A path scrolling through the screen cycles through
at most 16 sub-pixel variants instead of rendering a new mask per frame.

Eviction: masks unused for 240 frames are dropped; over 32 MB, the least recently used go first;
a mask a frame in flight may still read is never freed.

### Views and clips

- `PathShape(path, {.view_box, .fill_rule})` scales the path to fit the view and centers it (SVG's
  "xMidYMid meet").
- `CustomShape([](Rect r) { return path; })` builds the path for the frame size. It's rebuilt when
  the size changes.
- `Shape::stroke(color, StrokeStyle)` with dashes, on any shape, strokes through this pipeline.
- `GraphicsContext::fill_path` / `stroke_path` in a Canvas.
- **`clip_shape` with a path**: the content renders into a layer. Then a quad covering the
  whole layer is drawn with a **destination-in** blend through the path's mask, keeping only
  what the path covers. The sampler clamps to the mask's empty border, so everything outside the
  path clears. The layer then composites normally. Hit testing follows the path.

Analytic shapes (rounded rects, capsules, circles) never use this pipeline for fills or clips; the
ubershader does them exactly and cheaper.

## Rules

- Flatten once per geometry; place with scale and offset.
- Strokes are fills of a union; never compute stroke outlines' intersections.
- Cache masks by content and sub-pixel phase; never free one in flight.

## Edge cases

- **Thin strokes** rely on MSAA, which is softer than analytic coverage would be.
- **Huge paths** produce huge masks; skipping MSAA for small masks or tiling big ones are
  possible improvements.
- **Even-odd vs non-zero** only differ for self-overlapping fills; strokes always use non-zero.
