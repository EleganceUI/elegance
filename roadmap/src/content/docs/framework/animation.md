---
title: Animation and transitions
description: Curves and springs, explicit and implicit animation, layout animation, and transitions.
sidebar:
  order: 8
---

Animatable properties are interpolated on the UI thread each frame. Only the affected paint or
layout properties change, and the loop renders continuously only while something moves.

:::note[Depends on]
[If and ForEach](../dynamic-content/) and the transaction hooks in the [Reactive core](../reactive/).
:::

## Read first

- `design.md` §9.5 (Animation), M3
- `include/elg/animation.h`, `src/animation/animation.cpp`, `src/animation/frame_clock.cpp`, `src/view/animated.h`
- `tests/animation_tests.cpp`

## Tasks

### Curves
- [ ] `Animation::linear`, `ease_in`, `ease_out`, `ease_in_out` as CSS cubic Béziers, with a duration
- [ ] `Animation::spring(response, damping_fraction)` with SwiftUI's meaning (1 settles without overshoot); `smooth` (a gentle spring)
- [ ] `.delay(seconds)`; `progress(seconds)` returns 0 at the start and 1 at the end (springs overshoot)

### Frame clock and animated values
- [ ] `set_frame_time(now)` once per frame, so every animation samples the same instant
- [ ] `Animated<T>`: `set(target, animation*)` jumps (null) or eases from the *current* value; `current()`, `target()`, `animating()`
- [ ] Solid colors blend; anything involving a gradient switches to the target
- [ ] `shift(delta)`: move start and target together, keeping progress (scroll anchoring)

### Explicit and implicit
- [ ] `with_animation(animation, fn)`: a transaction; effects scheduled by the writes inside run with `current_animation()` set
- [ ] `.animation(animation, value)`: an `ImplicitAnimation` that "fires" when `value` changed in the same update; priority effects detect the change before other effects run
- [ ] Animatable setters take `(value, const Animation*)`; `bind` passes the transaction's animation, else the implicit one if it fired
- [ ] Animated: fills, strokes, text color, opacity, offsets, and element frames (layout)

### Layout animation
- [ ] `Element::place` eases a changed frame when the update was animated; a first placement always snaps
- [ ] `LayoutAnimationScope` carries the transaction's animation through layout
- [ ] `FullLayoutScope` while anything animates: elements move without being invalidated, so lay out everything
- [ ] Text whose *content* changed snaps to its new frame (glyphs can't morph); unchanged text still eases
- [ ] `.animates_layout(false)` makes a subtree snap positions and sizes; colors, opacity and transitions still animate

### Transitions
- [ ] `Transition::opacity` (the default), `scale(from)`, `offset`, `move(edge)`, `slide`, `combined`, `asymmetric`
- [ ] `TransitionElement`: insertions animate in; removals start an exit and keep painting until it ends
- [ ] Children animating out keep their z-index

### Pacing
- [ ] `DisplayList::animating` set by any element mid-animation: the window needs another frame
- [ ] While animating, render every frame paced by presents, then block again

## Done when

- [ ] A spring with `damping_fraction` 0.5 overshoots, and one with 1.0 doesn't (unit test the curves)
- [ ] Toggling a panel with `with_animation` slides and fades it, and siblings glide to their new places
- [ ] After animations settle, idle CPU returns to 0 (the design measured 0 ms over 4 idle seconds)

## Gotchas

- **Retargeting doesn't carry velocity yet**: interrupting a spring restarts it from rest at its
  current value. That is the top animation follow-up.
- Implicit animation triggers must run **before** the other effects in the same flush, or the
  properties they should animate have already jumped. That's what priority effects are for.
- Not built yet: `matched_geometry_effect`, keyframes / `PhaseAnimator`, gradient interpolation,
  rotation transforms, and transitions for lazy-stack items.
