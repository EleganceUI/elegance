---
title: Hit testing, events, and focus
description: "How pointer and keyboard input reach handlers, hover delays, capture, and keyboard focus."
sidebar:
  order: 6
depends: [framework/shapes-and-effects]
---

## What it's for

This part routes input. Pointer events go to the views under the pointer, keys go to the focused
view, and hover is reported without flicker. It also defines keyboard focus: what can take it,
how Tab moves it, and when the ring shows.

## How it works

### Hit testing

`Element::hit_test(point, path)` returns whether the point (window points) hits the element's
**content**, and fills `path` from that element down to the innermost element hit. The
semantics are SwiftUI's:

- a view is hit where it draws: a shape by its outline (a stroke-only shape on its line), text by
  its frame, a stack by any child;
- clips limit hits, and offsets move them;
- `.content_shape(shape)` replaces the hit area; `.allows_hit_testing(false)` removes the view
  and everything inside;
- among overlapping siblings, the one drawn on top (by z-index, then declaration order) is tested
  first.

### Handlers

An `EventModifier` makes one `EventHandler` per mounted view, held by an `EventElement`. Controls
like `ButtonElement` implement `EventHandler` themselves. A handler can respond to pointer
down/up/move (with button, click count and modifiers), hover enter/leave, keys, text input,
scrolling, and focus changes. Each method returns whether it **consumed** the event.

### The dispatcher

One `EventDispatcher` per window. Every method returns whether the window needs a new frame.

**Pointer buttons**: hit test, then offer the event to handlers along the path, innermost first,
until one consumes it. Consuming a **down** **captures** the pointer: the matching up goes to the
same handler, wherever the pointer is.

**Hover** is tracked as a list of handler-bearing elements under the pointer. Each entry has
`over` (is the pointer over it now), `reported` (what its handler was last told), and `due` (when
to tell it). When the pointer moves:

- elements newly under it get `over = true` and a due time of now + their **enter delay**;
- elements it left get `over = false` and a due time of now + their **exit delay**;
- if `over` flips back to `reported` before the due time, the pending change is cancelled.

`tick()` delivers changes that are due, leaves innermost first, then enters outermost first,
like the DOM. `next_deadline()` tells the event loop when to wake next, so it sleeps instead of
polling. The delay comes from the handler, else the environment's `HoverDelay` (enter 0.1 s, exit
0 by default). Passing quickly across a toolbar therefore lights nothing up.

After each frame, `refresh_hover()` re-tests at the last pointer position, because scrolling or
animation can move content under a still pointer.

**Tree changes**: when If/ForEach or lazy stacks remove elements, `revalidate()` drops entries
(hover, capture) for elements that no longer exist. It compares pointers without dereferencing
them, then re-evaluates hover.

### Keyboard focus

`FocusManager` (one per window, outliving the tree) holds the focused element and whether the
ring is visible.

- **Keys** go to the focused element's handler, then bubble out through its ancestors until one
  consumes them.
- **Tab / Shift+Tab**, if unconsumed, move focus to the next or previous focusable element in tree
  order, wrapping around. Focus by Tab is "navigated": text fields select all then.
- **Clicking** focuses the innermost focusable element under the pointer, without a ring, or
  clears focus if there's none. Inside a `.focusable(false)` scope (caption buttons), clicks
  leave focus alone.
- **The ring** shows for Tab and programmatic focus, never for clicks (like `:focus-visible`). An
  element joined to focus draws it, in the tint, around its frame.
- **Destruction**: an element that joined focus calls `forget(this)` in its destructor, so
  removing the focused row is safe.
- **Text input**: focusing an element that accepts text turns the platform's text input (IME)
  on, and leaving it turns it off.

`.focused(Binding<bool>)` uses `watch(scope, changed)`, which reports focus entering or leaving a
subtree. The binding is true while focus is anywhere inside. Setting it true focuses
`first_focusable(scope)`. The wrapper is a Tab stop only if nothing inside can take focus.

`KeyEvent` has a layout-mapped `Key`, `KeyModifiers` and `repeat`. `primary_modifier()` is Cmd on
macOS and Ctrl elsewhere.

## Rules

- Innermost first for pointers; focused-then-ancestors for keys.
- Never dereference an element the tree may have destroyed.
- Hover changes respect delays; nothing polls while idle.
- Handlers run with the current frame time set, so animations they start begin now.

## Edge cases

- **Address reuse**: an element allocated at a freed address in the same flush could be mistaken
  for the old one. Generation IDs would fix it if it ever matters.
- **Pointer leaving the window** sends leave events after the exit delays.
- **Disabled subtrees**: controls inside read `DisabledKey` and ignore input. Whether they should
  also skip Tab focus is worth deciding explicitly; native toolkits skip them.
- Not yet designed in detail: focus scopes and traps for modals, window-level shortcuts, and
  gesture recognizers (drag, magnify, rotate) with composition.
