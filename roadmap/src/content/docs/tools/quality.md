---
title: Tests, examples, and docs
description: Unit tests per subsystem, the sandbox, example apps, and the documentation site.
sidebar:
  order: 3
---

Write the tests alongside each subsystem rather than at the end. The examples and the docs site
are how anyone else (including future you) learns the API.

:::note[Depends on]
Each item depends on the subsystem it covers.
:::

## Read first

- `tests/*.cpp`, `apps/sandbox/`, `apps/login/`, `examples/*/main.cpp`, `docs/`

## Tasks

### Unit tests (Catch2)
- [ ] `reactive_tests`: diamond glitch-freedom, batching, untrack, disposal, cycles, UI-thread pinning
- [ ] `environment_tests`: inheritance, overrides, ThemeColor resolution, reactive keys
- [ ] `animation_tests`: curves, springs, transactions, implicit triggers
- [ ] `path_tests`: SVG parsing, bounds, contains, flattening, stroking
- [ ] `image_tests`: icon names, variants, default set, suggestions, each name-map format, registration errors, `ImageSource` validation, `resizable` / `aspect_ratio` misuse
- [ ] `audio_tests`, `video_tests`, `capture_tests`: sources before start, copies sharing, WAV headers, option clamping, device listing
- [ ] `http_tests`, `desktop_tests`, `accessibility_tests`
- [ ] A `plan_frame` test for clipping and layers (no GPU needed)
- [ ] Dogfooding tests: reimplement a few built-ins (`aspect_ratio`, `border`) with only the public API and compare results

### Apps
- [ ] `sandbox`: a kitchen-sink app exercising every feature, with generated assets (landscape JPEG, PNG with alpha @2x, EXIF-rotated JPEG, click/tune WAVs, a 10 s test clip)
- [ ] `apps/login`: a web-style sign-in screen (gradient backdrop, custom styles, validation, `.focused`, a QR panel drawn with Canvas)
- [ ] Examples: `hello`, `counter`, `layout`, `styling`, `animation`, `drawing`, `todos`, `popover`, `title_bar`, `desktop`, `network`, `media_player`, `recorder`

### Docs site
- [ ] Astro Starlight in `docs/`: start pages (introduction, installation, first app, concepts) and guides for each area
- [ ] API reference generated from `include/elg/` by `scripts/gen-reference.mjs` on each build
- [ ] Guides embed the examples' sources and screenshots
- [ ] GitHub Pages workflow (`.github/workflows/docs.yml`)

## Done when

- [ ] `ctest` passes in debug and release
- [ ] Every example builds and runs
- [ ] The docs build without warnings and every guide's code compiles as an example

## Gotchas

- Not built yet: a headless mode (offscreen render target) for screenshot and golden-image tests,
  synthetic input for UI tests, and lavapipe for CI.
