---
title: Tests, examples, and docs
description: "What gets unit tested and how, what the sandbox and examples are for, and how the docs site is generated."
sidebar:
  order: 3
depends: [foundations/build]
---

## What it's for

A framework is only as trustworthy as the evidence that it works, and only as usable as its
examples and docs. This part defines three layers: unit tests for logic, apps for whole-system
behavior, and the docs site for learning the API.

## How it works

### Unit tests

Catch2, behind the `tests` vcpkg feature, registered with CTest. Tests may include internal
headers. What's worth unit testing is **logic without a GPU or window**:

| Area | What the tests check |
|---|---|
| Reactive | Glitch freedom (diamonds), batching, untrack, disposal (including queued effects), cycle detection, UI-thread pinning |
| Environment | Inheritance, overrides, ThemeColor resolution, reactive keys |
| Animation | Curve values at known points, spring settling, transactions, implicit triggers |
| Paths | SVG parsing (including packed arc flags and error positions), bounds, contains, flattening, stroking |
| Images and icons | Source validation, `resizable`/`aspect_ratio` misuse, icon name resolution, variants, suggestions, every name-map format, registration errors |
| Media | Requests before start applied later, copies sharing state, WAV headers and durations, option clamping, device listing |
| HTTP, desktop, accessibility | Header lookup, cancellation, the rules around dialogs and drops, element semantics |
| Frame planning (to write) | Clip resolution and culling, layer ordering (pure, so no GPU needed) |

Views can be built in tests without an App (the reactive core allows any thread before `run()`),
so view construction and usage errors are testable too.

**Dogfooding tests** (called for by the design, not written yet) re-implement a few built-in
modifiers (`aspect_ratio`, `border`) using only the public protocols and compare their layout and
display lists with the built-ins. They'd prove the "no private path" rule rather than rely on it.

### Apps

- **Sandbox** (`apps/sandbox`): a kitchen-sink app that exercises every feature, with generated
  assets: a landscape JPEG, a PNG with alpha at @2x, a JPEG stored sideways with EXIF orientation 6,
  click and tune WAVs, a 10 s H.264/AAC test clip. It's where a person checks visual results.
- **Login** (`login-cpp` in elegance-examples): one realistic screen built only from the public API, with custom
  styles, validation and focus handling, and a QR code drawn with Canvas.
- **Examples** (elegance-examples, `<name>-cpp` and `<name>-rs`): small, focused programs, each the source of a docs page's code
  and screenshot.

### The docs site

`docs/` is an Astro Starlight site:

- **Guides** written by hand, per area, embedding example sources and screenshots.
- An **API reference** generated on every build from `include/elg/` by
  `scripts/gen-reference.mjs`, so it can't drift from the headers.
- Published to GitHub Pages by `.github/workflows/docs.yml`.

## Rules

- Logic gets a unit test when it's written, not later.
- Every guide's code compiles as an example.
- The API reference is generated, never edited by hand.

## Edge cases

- **Rendering correctness** isn't covered by unit tests. A headless mode (offscreen render target)
  for golden-image tests, with lavapipe in CI, is the missing layer.
- **Input-driven behavior** needs a synthetic input API to test without a person.
