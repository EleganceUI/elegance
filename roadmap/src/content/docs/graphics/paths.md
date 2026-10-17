---
title: GPU vector paths
description: Path geometry and stroking, stencil-then-cover coverage masks, mask caching, and path clips.
sidebar:
  order: 1
---

Arbitrary shapes are rasterized on the GPU: flatten on the CPU, count windings into a
multisampled stencil, cover, resolve to an anti-aliased R8 mask, then fill through the mask in the
UI shader.

:::note[Depends on]
[Display list and the UI ubershader](../../renderer/ui-pipeline/) and [Shapes, decorations, and effects](../../framework/shapes-and-effects/).
:::

## Read first

- `design.md` §5 (Vector paths), "GPU vector paths", "Path mask caching", "`clip_shape` with any path shape"
- `include/elg/path.h`, `src/geometry/path.cpp`, `src/gpu/path_renderer.*`, `shaders/path*.{vert,frag}`, `tests/path_tests.cpp`

## Tasks

### Geometry
- [ ] `Path` building: move, line, quad, cubic, close, SVG-style arcs; rect, rounded-rect, ellipse and circle helpers
- [ ] `Path::svg(d)`: every command, relative and absolute, packed arc flags; errors report the position
- [ ] `bounds()`, `contains(point, FillRule)`, `transformed(...)`
- [ ] `flatten(tolerance)` using Wang's formula for segment counts
- [ ] `stroked(StrokeStyle)`: width; butt/round/square caps; miter/round/bevel joins with a miter limit; dash patterns with a phase
- [ ] Emit the stroke as a union of consistently wound pieces (segment quads, joins, caps) that non-zero merges, so self-intersections need no special handling

### Rendering
- [ ] `PathCommand`: shared flattened polygons in local points, placed by `scale` and `offset`, so scrolling and transitions never re-flatten
- [ ] The planner turns each path into fan triangles plus a mask pass (`PlanPath`)
- [ ] `PathRenderer`: winding counts into an MSAA stencil (increment/decrement-wrap by facing for non-zero, invert for even-odd), cover where inside, resolve to R8
- [ ] Stencil format: the first supported of S8 / D24S8 / D32S8; up to 8× samples
- [ ] `ui.frag` `kind = path`: fill color or gradient × mask coverage

### Mask cache
- [ ] Key masks by a hash of polygons, scale, sub-pixel offset (snapped to ¼ px), size and fill rule
- [ ] Drop masks unused for 240 frames; LRU over 32 MB; never free one still in flight

### Views and clips
- [ ] `PathShape(path, {.view_box, .fill_rule})`, fitted and centered like SVG's "meet"
- [ ] `CustomShape([](Rect r) { return Path...; })`, rebuilt per frame size
- [ ] `Shape::stroke(color, StrokeStyle)`: dashed strokes on any shape go through paths
- [ ] Hit testing on the flattened fill or stroke
- [ ] `GraphicsContext::fill_path` / `stroke_path`
- [ ] `clip_shape(PathShape | CustomShape)`: render content to a layer, multiply it by the mask with a "destination-in" UI-pipeline variant (mask quad covers the layer; sampler clamps to an empty border), then composite. Rounded-rect shapes keep the analytic clip

## Done when

- [ ] `tests/path_tests.cpp`-style tests pass for SVG parsing, bounds, contains, and stroking
- [ ] A dashed rounded-rect stroke and an SVG heart render crisply at 100% and 200%
- [ ] Scrolling a page of paths renders each mask once and none after (the design measured 7 masks for a 75-frame scroll)

## Gotchas

- Avoid per-pixel curve evaluation (Slug-style); it's patented.
- Follow-ups: skip MSAA for small masks under memory pressure; analytic-coverage AA for sharper
  thin strokes; a Vello-style compute rasterizer if path-heavy scenes need it; SVG files and SVG
  icon sets.
