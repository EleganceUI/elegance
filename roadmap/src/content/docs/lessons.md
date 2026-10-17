---
title: Lessons learned
description: Every trap the current code already fell into, in one place.
---

These cost real debugging time in the existing code. Each also appears on the page for its
subsystem; this page collects them so you can skim them all before you start.

## Architecture rules that are hard to retrofit

- **A body runs once.** Everything dynamic is a reactive argument, `If`, or `ForEach`. Don't build
  a diffing step, because you won't need one.
- **Cost proportional to the change, at every stage.** Reactivity and layout already are; paint,
  GPU and present are not yet. Give every paint-affecting setter an `invalidate_paint()` call from
  the start, so [incremental rendering](../renderer/incremental/) can be added without touching
  every element again.
- **The UI thread never touches Vulkan; the render thread never touches elements or signals.**
  Frames cross as immutable packets holding copies.
- **Copy, never borrow** strings and values from user code; **stable string keys**, never
  `typeid` or addresses; **every effect belongs to a scope**. These three are what make hot
  reload possible.
- **Destroy a scope before the elements its effects update.** Get member declaration order right
  wherever both live together.
- **Every modifier goes through `View::modifier()`.** Built-ins get no private path, so users can
  do anything the framework does.

## Build and C++

- From a plain shell, MSVC can't find `cstdint`: load `vcvars64.bat` first.
- Export `View` and `Shape` per member, not per class, or app-defined extension methods fail with
  C2491.
- Use `OptionalLength`, not `std::optional<float>`, in option structs; int literals otherwise warn
  at `/W4`.
- Make `State`'s constructor `explicit`, so `count = 1` assigns.
- **Qualify `detail::bind(...)`.** ADL picked `std::bind` for `Prop<std::string>` rvalues and
  silently dropped bindings.

## Frame loop

- Clear `dirty` **before** building a frame, or redraws requested during layout are lost.
- Write `if (call()) dirty = true;`, never `dirty = call();`.
- Event handlers must see the current frame time, or animations they start begin in the past.
- Re-evaluate hover after every frame, because content moves under a still pointer.
- Bound every UI-thread wait on the render thread with a timeout.

## Windows and presentation

- SDL's live-resize timer is a `WM_TIMER` and fires irregularly during fast drags. Also draw on
  every size change inside the move/size loop (`GUI_INMOVESIZE`), then `DwmFlush`.
- Windows Vulkan drivers only offer opaque composite alpha. Transparency (and stretch-free
  resizing) goes through DirectComposition with a D3D11 texture shared into Vulkan.
- Composition swapchains present with interval 0; interval 1 adds a frame of hover latency.
- Retire old swapchains until a later submission completes; a pending present may still use their
  semaphores. Keep one `render_finished` semaphore per image.

## Rendering

- Intersecting a rounded clip with a rect clip while keeping the radii gives a lens shape. Carry
  the innermost rounded clip and a separate scissor, then multiply the coverages.
- Give single shapes fast paths for opacity and shadow; offscreen layers for everything else.
- Straight-alpha mipmaps fringe at transparent edges.
- Do the YUV→RGB math through an sRGB view so it stays in gamma space.

## Text

- Read font files in one bulk read; a stream iterator took ~1 s per CJK font in debug.
- The debug heap's single lock made 8 glyph workers 2.5× slower than 3.
- Icon fonts need a single-channel distance field; multi-channel fields turn overlapping
  outlines into speckles.
- Reverse PostScript-direction outlines (Phosphor rendered inside-out).
- A persistent glyph cache took debug startup from 2.17 s to 0.68 s.

## Layout and lists

- Spacers must take only what content leaves, or `HStack { Text; Spacer }` truncates text that
  fits.
- Sum lazy-list sizes in `double`; `float` drifted a whole row over 10,000 items.
- Key measured sizes by item position with a running total: 16 ms → 1.6 ms per frame for 10,000
  rows.
- Anchor scrolling on the first visible row when sizes above it change.

## Focus and input

- `.focused(binding)` must not add its own Tab stop when something inside can take focus.
- Elements must release focus in their destructor.
- The dispatcher compares addresses; a reused address in the same flush can impersonate a dead
  element. Generation IDs fix it.

## Media

- DirectShow: don't force RGB32 (it inserts converters virtual cameras never feed), run the graph
  without a reference clock (the Null Renderer stalls otherwise), and declare the Sample Grabber
  interfaces yourself (`qedit.h` is gone from the SDK).
- GPU YUV conversion: 0.17 cores vs 1.25 on the CPU for 1080p60.
- The Media Foundation event callback must never take the source's lock.
- Audio callbacks must never allocate or lock.
