---
title: Animation and transitions
description: "Timing curves and springs, explicit and implicit animation, animated properties and layout, and transitions."
sidebar:
  order: 8
depends: [framework/dynamic-content]
---

## What it's for

Animation in Elegence is a property of **updates**, not of views. You say *this change should
animate*, and every property the change affects (a color, an opacity, a frame, a view's
insertion) eases from where it is to where it should be. Nothing animates continuously unless
something is moving.

## How it works

### Curves

An `Animation` maps elapsed seconds to progress (0 at the start, 1 at the target):

- **Bézier curves** (`linear`, `ease_in` (0.42, 0, 1, 1), `ease_out` (0, 0, 0.58, 1),
  `ease_in_out` (0.42, 0, 0.58, 1)) over a duration, as in CSS. Progress solves x(t) = elapsed /
  duration for t, then returns y(t).
- **Springs** (`spring(response, damping_fraction)`), with SwiftUI's meaning. ω = 2π / response,
  ζ = damping fraction, released from 0 toward 1 at rest:
  - ζ < 1 (bouncy): `1 − e^(−ζωt)·(cos(ω_d t) + (ζω/ω_d)·sin(ω_d t))`, with ω_d = ω√(1−ζ²)
  - ζ = 1 (critical): `1 − e^(−ωt)·(1 + ωt)`
  - ζ > 1 (overdamped): the two-exponential solution.
  A spring counts as finished once its slowest-decaying term is below a small tolerance.
- `.delay(seconds)` shifts the start.

### Animated values

Every animatable element property is an `Animated<T>`: a `from`, a `to`, an optional animation
and a start time. `set(value, animation)` either jumps (null animation) or starts easing from the
**current** value to the new target. `current()` evaluates at the **frame time**, set once per
frame, so every animation in a frame samples the same instant. Interpolation is per type: floats
and rects lerp; solid colors lerp in their components; anything involving a gradient switches to
the target.

### Where the animation comes from

A reactive argument's effect applies its value with an animation, or with none:

1. **Explicit**: `with_animation(animation, fn)` opens a transaction. Writes inside it stamp the
   effects they queue with the animation. When those effects run, `current_animation()` returns
   it, and the setter they call passes it to `Animated::set`. Everything that changes as a
   consequence of those writes animates: colors, opacity, offsets, layout, insertions and removals.
2. **Implicit**: `.animation(animation, value)` creates an `ImplicitAnimation` for the subtree. A
   **priority effect** watches `value`; when it changes, it marks the implicit animation as
   "fired" for the current flush. Effects for properties inside the subtree that run later in the
   same flush see it fired and animate. Being a priority effect is essential: the trigger must run
   before the property effects in that flush.

### Layout animation

Elements' frames are `Animated<Rect>`. When a layout runs because of an animated update, the
app sets a `LayoutAnimationScope` with the update's animation (taken with
`take_flush_animation()`). `Element::place` then eases a changed frame there instead of jumping.
Special cases:

- An element's **first** placement always jumps.
- **Text whose content changed** snaps to its new frame (`snap_next_layout`): glyphs can't morph,
  so a gliding position looks wrong. Text that merely moved still eases.
- `.animates_layout(false)` makes a subtree's frames always snap; colors, opacity and transitions
  still animate.

While anything animates, frames move without any element being invalidated. The app therefore
lays out the whole window under a `FullLayoutScope` on those frames.

### Transitions

Each If/ForEach child is wrapped in a `TransitionElement`. A `Transition` describes how a view
looks at progress p (0 = fully out, 1 = in place): `opacity` (the default), `scale(from)`,
`offset`, `move(edge)` (beyond an edge of its frame), `slide` (in from leading, out to trailing),
`combined`, and `asymmetric(insertion, removal)`. Insertion runs p from 0 to 1 with the update's
animation, and removal from 1 to 0 while the element stays in the fragment's `exiting_` list.
The element applies the transition to its painted commands (`transform` for move and scale,
opacity as a group).

### Pacing

Any element mid-animation sets `DisplayList::animating` while painting. Then the loop renders the
next frame as soon as the previous one was presented (vsync pacing). When nothing animates, the
loop goes back to blocking. Idle CPU is 0.

## Rules

- Animations are attached to updates; views only declare what's animatable.
- One frame time per frame.
- First appearance never animates its frame.
- Stop requesting frames the moment nothing moves.

## Edge cases

- **Retargeting**: a new target mid-animation restarts from the current value **at rest**.
  Springs should carry their velocity instead; that's the main follow-up.
- **Children exiting** keep their z-index while they fade.
- **Gradients** don't interpolate; they switch.
- Not built: rotation transforms, `matched_geometry_effect`, keyframes / `PhaseAnimator`.
