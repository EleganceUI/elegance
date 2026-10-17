---
title: Hit testing, events, and focus
description: Pointer routing, hover delays, keyboard focus, Tab traversal, and the focus ring.
sidebar:
  order: 6
---

Input is routed through the element tree. Pointer events go innermost-first to handlers under
the pointer; keys go to the focused element and bubble out through its ancestors.

:::note[Depends on]
[Shapes, decorations, and effects](../shapes-and-effects/).
:::

## Read first

- `design.md` §10 (Events & Input), M1 hit testing and hover, M4 keyboard and focus
- `include/elg/events.h`, `include/elg/modifiers/events.h`
- `src/view/event_dispatcher.*`, `src/view/focus.h`, `src/view/events.cpp`, `src/view/window_services.h`

## Tasks

### Hit testing
- [ ] `Element::hit_test(point, path)`: a view is hit where its content draws. Shapes use their outline, stroke-only shapes their line, text its frame
- [ ] Clips are respected; `.content_shape(Shape)` overrides the area; `.allows_hit_testing(false)` opts out
- [ ] Offsets move the hit area with the drawing

### Events
- [ ] `EventHandler` interface: pointer (down/up/move with button, click count, modifiers), hover enter/leave, key, text input, scroll, focus changes
- [ ] `EventModifier::make_handler()` makes one handler per mounted view
- [ ] `EventDispatcher` per window: down/up bubble innermost-first until consumed; consuming a down **captures** the pointer until the up
- [ ] `.on_tap_gesture(count, action)` (single and double), `.on_hover(action)`

### Hover delays
- [ ] `HoverDelay{.enter, .exit}`: hover starts after the pointer rests for `enter` (default 0.1 s) and ends `exit` after it leaves (default 0)
- [ ] Passing over, or coming back in time, cancels the pending change
- [ ] `.hover_delay({...})` for a subtree (environment) or per handler
- [ ] `tick()` delivers due changes; `next_deadline()` lets the idle loop wait with a timeout and otherwise stay at 0% CPU
- [ ] `refresh_hover()` after each frame, since content may move under a still pointer (scrolling)

### Keyboard focus
- [ ] One `FocusManager` per window. Keys go to the focused element, then bubble to ancestors until consumed
- [ ] Unconsumed Tab / Shift+Tab moves through focusable elements in tree order, wrapping around
- [ ] Pressing the pointer focuses the innermost focusable element under it, or clears focus
- [ ] Focus ring in the tint, shown for Tab and programmatic focus only (like `:focus-visible`)
- [ ] A destroyed element lets go of focus in its destructor (`forget`)
- [ ] `.focusable()`, `.focusable(false)` (takes a whole subtree out), `.focused(Binding<bool>)`, `.on_key_press(Key, action)` and `.on_key_press(fn -> bool)`
- [ ] `KeyEvent`: layout-mapped `Key`, `KeyModifiers`, `repeat`; `primary_modifier()` is Cmd on macOS and Ctrl elsewhere
- [ ] `FocusManager::watch(scope, changed)` and `first_focusable(scope)` for `.focused`

### Tree changes
- [ ] After If/ForEach add or remove elements, `revalidate()` forgets elements that no longer exist (without touching them) and re-evaluates hover

## Done when

- [ ] A button inside a clipped scroll area isn't clickable where it's clipped away
- [ ] Tab cycles through three text fields and wraps; the ring shows for Tab, not for clicks
- [ ] Removing the focused row from a list doesn't crash, and focus is cleared
- [ ] Hovering quickly across a toolbar doesn't flash every button's hover state

## Gotchas

- **`.focused(binding)` must follow SwiftUI**: true while focus is anywhere inside; setting it
  focuses the first focusable view inside; the wrapper is a Tab stop only when nothing inside can
  take focus. The first version made the wrapper an extra Tab stop, so Tab into a wrapped field
  landed on an invisible element and typing went nowhere.
- Event handlers must see the current **frame time**, or animations they start (scrolling, scroll
  bars) begin in the past and jump.
- The dispatcher compares element addresses, so a new element allocated at a freed address in the
  same flush could be mistaken for the old one. Use generation IDs if that ever bites.
- Not built yet: focus scopes and traps for modals, window-level shortcuts, a command registry,
  drag/magnify/rotate gestures, gesture composition, `GestureState`, and synthetic input for tests.
