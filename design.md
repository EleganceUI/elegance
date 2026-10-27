# Elegance — Design Document

> Status: **Draft / living document**
> Last updated: 2026-10-08

Elegance is a C++ UI framework for building modern app interfaces in a SwiftUI-style declarative
API (views composed from stacks, styled with chained modifiers like `.padding()`, `.background()`,
`.font()`, `.clip_shape()`, and driven by reactive state) with a GPU-native Vulkan renderer. It
aims for web-like productivity without a browser engine's memory, startup time, and runtime cost.

Sections marked **TODO** or **OPEN** still need decisions. Each proposed default is a starting
point, not a commitment.

---

## 1. Goals & Non-Goals

### Goals
- **Fast to author**: building a UI should feel like SwiftUI. You compose views and chain
  modifiers, and appearance lives next to structure in the code.
- **Stateful by design**: reactive state is built into the core rather than added on later.
  Changing state updates only what depends on it.
- **Performance**: start in under 100 ms, idle near 0% CPU/GPU, and keep a 1000+ element UI under
  2 ms CPU per frame. Memory should be tens of MB, not hundreds.
- **Cross-platform**: Windows, Linux (X11 + Wayland), and macOS (via MoltenVK). Mobile is a
  stretch goal.
- **Batteries included**: a rich component library, text editing, animation, theming, dev tools,
  and accessibility.
- **Embeddable**: usable as a standalone app framework and as an overlay inside an existing
  Vulkan app (games, tools).

### Non-Goals
- HTML/CSS/JS compatibility of any kind. There is no CSS cascade and no DOM API.
- A 1:1 SwiftUI port. We borrow the authoring model and layout semantics, and adapt them to C++.
- **XML or any other markup/UI description format** (no XAML/QML/.ui/.slint-style files). UI
  is C++ code only. Fast iteration comes from DLL hot reload instead (§9.6).
- A scripting layer of any kind (JS, Lua, WASM). Logic is C++. Iteration speed comes from DLL hot
  reload (§9.6).
- Rendering arbitrary web content.
- **OPEN**: Native-looking widgets (OS-themed)? Proposed: no. Elegance draws everything itself.

---

## 2. Architecture Overview

```
┌──────────────────────────────────────────────────────────────┐
│  Application code (views + modifiers, C++)                   │
├──────────────────────────────────────────────────────────────┤
│  Component Library   (Button, TextField, List, Sheet, ...)   │
├──────────────────────────────────────────────────────────────┤
│  View Builder        (View values + modifier chains, lowered │
│                       into the element tree on mount)        │
├──────────────────────────────────────────────────────────────┤
│  Reactive Core       (State, Binding, computed, effects)     │
├──────────────────────────────────────────────────────────────┤
│  Element Tree        (retained nodes, paint props, events)   │
├───────────────┬──────────────────┬───────────────────────────┤
│  Environment  │  Layout Engine   │  Event / Focus / Input    │
│  (font, color,│  (propose/size/  │  (hit test, bubbling,     │
│   theme, inh- │   place: stacks, │   keyboard nav, IME,      │
│   erited vals)│   frame, grid)   │   gestures)               │
├───────────────┴──────────────────┴───────────────────────────┤
│  Display List        (immutable draw commands per layer)     │
├──────────────────────────────────────────────────────────────┤
│  Renderer            (Vulkan: batching, SDF shapes, text,    │
│                       images, clipping, compositing)         │
├──────────────────────────────────────────────────────────────┤
│  Platform Layer      (window, surface, input, clipboard,     │
│                       IME, DPI, cursors, file dialogs)       │
└──────────────────────────────────────────────────────────────┘
```

**Frame pipeline:**

`input → state updates → (dirty) environment → (dirty) layout → (dirty) paint → display list → GPU submit`

Each stage runs only on the dirty subset of the tree. If nothing is dirty, no frame is produced.

---

## 3. Language & Build

### 3.1 Language standard: C++23 (decided)
- `CMAKE_CXX_STANDARD 23`, required. C++26 was considered but isn't usable yet. Probed
  2026-10-08:
  - MSVC 19.51 (VS 18) supports none of the C++26 features tested: reflection, contracts,
    `#embed`, pack indexing, `= delete("reason")`, `<inplace_vector>`, `std::execution`,
    `template for`.
  - CLion's bundled GCC 15.2 has only `#embed`, pack indexing, and `= delete("reason")`.
- C++23 features the design relies on:
  - **Deducing `this`**, for modifier chaining on specific view types.
  - **`std::expected`**, for `try_` APIs (§3.2).
  - **`std::print`**.
  - **`std::stacktrace`**, for crash reports and debug-build exception traces. MSVC supports it
    as-is; GCC needs `-lstdc++exp`.
  - **`std::move_only_function`**, for closures in views and handlers.
  - **`std::flat_map`**, `std::mdspan`, `std::ranges::to`, `if consteval`, and
    `[[assume]]`.
- Workarounds for missing C++26 features:
  - *Reflection*: `Store<T>` fields are declared with a macro (`ELG_STORE(T, field...)`), and
    stable type keys use explicit names (§9.6, rule 2).
  - *`#embed`*: WGSL shaders (as string literals) and prebuilt font atlases are embedded through
    a CMake step that generates sources.
  - *`std::inplace_vector`*: use a small in-house `SmallVector` for modifier chains.
- Revisit when MSVC, GCC, and Clang all ship reflection. Then `Store<T>`, inspector panels, and
  theme-file binding can drop their macros.

### 3.2 Error handling (decided)
There are two classes of failure, with different handling.

**1. Developer-facing errors propagate as exceptions.** These are caused by the developer's code
or input, or by the environment.
- The hierarchy is `elg::Error : std::runtime_error`, with these subclasses:
  - `ResourceError`: a font, image, or file failed to load.
  - `GpuError`: no suitable device, out of memory, or device lost and unrecoverable.
  - `PlatformError`: an SDL or window failure.
  - `UsageError`: API misuse detected at runtime, e.g. writing `State` from a non-UI thread or
    mounting a view twice.
- In debug builds, each error captures a `std::stacktrace` at the throw site.
- **Synchronous API calls** throw directly. Where failure is routine, a `try_` variant returns
  `std::expected` instead (e.g. `Font::try_load`).
- **Exceptions from user code inside framework callbacks** (`body()`, event handlers, effects,
  `on_appear`, `task`):
  - The framework catches them at the dispatch boundary and discards the in-progress frame, so
    the tree stays in its last consistent state.
  - The `std::exception_ptr` is then forwarded to `app.on_error(handler)`. The handler returns
    `ErrorAction::resume` (e.g. after showing a toast) or `ErrorAction::quit`.
  - With no handler, `App::run()` stops the loop, tears down cleanly, and **rethrows to the
    caller**, so `try { app.run(); } catch (...) {}` works.
- **Async**: a task's exception goes to its `Resource<T>` error state if one is observing.
  Otherwise it goes to `on_error`.
- **Render thread**: on device lost, it tries to recover by recreating the device and its
  resources from the UI-side handles. If that fails, it marshals a `GpuError` to the UI thread,
  which delivers it to `on_error`.
- **No unwinding through C frames**: every C callback (SDL event watch, Vulkan debug messenger,
  AccessKit) is a catch-all trampoline that marshals the exception instead.

**2. Internal errors produce a crash report, then terminate.** These are bugs in Elegance
itself, which the developer can't handle meaningfully, and continuing would risk corrupted state.
- **Sources**:
  - `ELG_ASSERT(cond, fmt, ...)` (always on, cheap checks) and `ELG_DEBUG_ASSERT` (debug only).
  - Unreachable states.
  - Validation-layer errors on framework-owned Vulkan objects (debug builds).
  - Hard faults: access violation/SIGSEGV, `std::terminate`, and pure virtual calls.
- **Classification rule**: `elg::Error` and `std::bad_alloc` thrown from framework internals
  propagate. Any *other* exception escaping framework internals (e.g. `std::out_of_range` from a
  bad index) is treated as a bug and produces a crash report.
- **Report contents**:
  - The message and source location, plus a `std::stacktrace`.
  - The Elegance version and build hash, the OS, and the GPU, driver, and Vulkan version.
  - Window and DPI state.
  - The last ~200 log lines from an in-memory ring buffer.
  - Recent input events and frame packet summaries.
  - Loaded hot-reload modules.
- **Output**: written to `SDL_GetPrefPath(...)/crashes/<timestamp>.txt`, plus a minidump on
  Windows.
  - `app.on_crash(fn)` gets the report path afterwards, for a custom dialog or upload.
  - The default shows `SDL_ShowSimpleMessageBox` with the path.
- **Debug builds** break into the debugger first if one is attached.
- **Hard faults** can't be trusted to in-process handlers after memory corruption. **OPEN**:
  crashpad (out-of-process minidumps) behind the `crash-reports` vcpkg feature. sentry-native is
  an alternative if upload is wanted. The in-process reporter always handles assertions.
- **RTTI**: left on (the compiler default). The framework doesn't use `typeid`/`dynamic_cast`
  in hot paths or for keys.

### 3.3 Build & packages (decided: vcpkg)
- CMake + **vcpkg in manifest mode**: a `vcpkg.json` at the repo root with a pinned
  `builtin-baseline`.
- **Optional features**: `tests` (Catch2), `profiling` (Tracy), and `crash-reports` (crashpad).
- **Toolchain** is wired through `CMakePresets.json`
  (`CMAKE_TOOLCHAIN_FILE=$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`). CLion picks
  presets up automatically.
- **Triplets**: `x64-windows` (dynamic CRT `/MD`, which hot reload requires per §9.6 rule 4),
  `x64-linux`, and `arm64-osx`.
- **Shaders**: WGSL (`shaders/*.wgsl`), embedded as string literals by `cmake/ElgShaders.cmake`.
  WebGPU compiles them when pipelines are created, so the build needs no shader compiler.
- **Library type**: `SHARED` by default; `ELG_BUILD_SHARED=OFF` builds it static (the
  `windows-release-static` / `macos-release-static` presets). Static builds skip the hot reload
  host, and `elegance_hot_reload()` refuses them (§9.6). The ABI surface uses `ELG_API` exports and
  pImpl at boundaries.
- **Namespace**: `elg::` (decided). Macros use an `ELG_` prefix.
- **Local setup**: `cmake/ElgBootstrap.cmake` runs before `project()` and picks vcpkg: the
  toolchain file passed in, then `VCPKG_ROOT`, then a clone it manages in the user's cache folder,
  updated to `vcpkg.json`'s `builtin-baseline`. It also fetches Ninja when it isn't installed.
  Per-machine presets go in the gitignored `CMakeUserPresets.json`.
- **Runtime DLLs**: vcpkg only deploys an executable's direct dependencies. So `elegance` copies
  its own runtime DLLs (SDL3, …) into the shared `bin/` folder after each build.

### 3.4 License: MPL-2.0 (decided)
- Elegance is licensed under the **Mozilla Public License 2.0** (`LICENSE` at the repo root).
- **Intent**: no closed, paid versions of the framework. Anyone who distributes a modified
  Elegance must publish their changes to Elegance's files under MPL-2.0.
- **Apps built with Elegance** can use any license, including closed-source and paid. MPL is
  file-level copyleft, so the app's own files are unaffected, and it works with static linking
  and inlined header code (unlike LGPL).
- MPL-2.0 can't forbid *selling*, and no open-source license can. It makes a closed paid fork
  impossible, though someone could still sell builds of the open code.
- **Source headers**: each source file starts with the MPL notice:
  ```cpp
  // This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
  // If a copy of the MPL was not distributed with this file, You can obtain one at
  // https://mozilla.org/MPL/2.0/.
  ```
- **Dependency compatibility**: SDL3 (zlib), volk (MIT), VMA (MIT), FreeType (FTL),
  HarfBuzz (MIT), and msdfgen (MIT) are all compatible.

### 3.5 Dependencies (via vcpkg)

| Area              | Candidate                               | Notes                                |
|-------------------|-----------------------------------------|--------------------------------------|
| Windowing/input   | **SDL3** (decided)                      | IME, touch, clipboard, dialogs, DPI  |
| GPU API           | **Dawn** (WebGPU, decided)              | D3D12/D3D11, Vulkan, Metal backends  |
| Shader compile    | WGSL embedded; Tint compiles at startup | No offline compiler or SDK needed    |
| Font loading      | FreeType                                | Outlines, metrics, color-glyph bitmaps |
| Glyph generation  | **msdfgen** (decided: MTSDF)            | See §5 Text rendering                |
| Text shaping      | HarfBuzz                                | Required for ligatures, complex text |
| Bidi / line break | SheenBidi / ICU4X / libunibreak         | **OPEN**                             |
| Images            | stb_image / libspng / libwebp           | SVG: lunasvg or resvg (**OPEN**)     |
| Layout            | Custom (SwiftUI-style protocol)         | See §7. Yoga/Taffy no longer a fit   |
| Accessibility     | AccessKit (C bindings)                  | See §12                              |

---

## 4. Platform Layer

Responsibilities: window creation, Vulkan surface, event pump, high-DPI/scale changes, clipboard,
drag & drop, IME composition, cursors, native file dialogs, multi-window, and the system theme
(light/dark).

- **Decided: SDL3.** It covers windows, Vulkan surfaces (`SDL_Vulkan_CreateSurface`), input,
  IME, clipboard, drag & drop, cursors, file dialogs, display scale, and the system theme on all
  targets.
- SDL stays behind an internal `platform/` interface. No SDL types appear in public headers, so
  a native backend could replace it later if needed (e.g. custom title bars, finer frame pacing).

### 4.1 Window customization (implemented in M4)

- `WindowDesc` fields:
  - `title_bar = TitleBar::hidden`: no system title bar or frame (`SDL_WINDOW_BORDERLESS`). On
    Windows 11 the corners are still rounded (DWM corner preference) unless the window is
    transparent.
  - `transparent`: `SDL_WINDOW_TRANSPARENT`, a clear default background, and a swapchain that
    prefers premultiplied composite alpha.
  - `opacity`: whole-window, via `SDL_SetWindowOpacity`.
  - `min_size`.
- Undecorated windows get an SDL hit test, called on the UI thread during event pumping:
  - 6 pt edges resize (unless maximized or not resizable).
  - Otherwise the innermost event handler under the point decides: a `.window_drag_area()`
    marker means *drag* (so double-click maximizes and Aero Snap works), and anything else (a
    button inside the bar) means *normal*.
- `WindowHandle` comes from `Environment<WindowKey>()`:
  - Actions: `close()` (posts a close request, same as the system button), `minimize()`,
    `toggle_maximized()`.
  - Tracked reads: `is_maximized()` and `is_active()`, fed by SDL window events.
  - The state outlives the window, so handles kept by an app go inert.
- `WindowControls({.style, .minimize, .maximize})`:
  - Windows-style caption buttons, 46×32, with a red hover on close. They use Segoe MDL2 Assets
    glyphs when the font is installed, with drawn fallbacks otherwise.
  - macOS-style traffic lights.
  - Both dim while the window is inactive, and both are `.focusable(false)`.
- `.focusable(false)` takes a whole subtree out of keyboard focus (Tab skips it, clicks leave
  focus alone).
- **Transparency on Windows goes through DirectComposition** (`gpu/composition_win32`).
  - Windows Vulkan drivers expose only `VK_COMPOSITE_ALPHA_OPAQUE`; on this AMD machine
    `SDL_WINDOW_TRANSPARENT` alone gave black corners.
  - Each transparent window gets a D3D11 device on the same adapter (matched by LUID).
  - It renders into a shared texture: created by D3D11 with an NT handle, imported into Vulkan
    via `VK_KHR_external_memory_win32`, a UNORM image with an sRGB view.
  - At the end of the frame the image is released to `VK_QUEUE_FAMILY_EXTERNAL` in GENERAL
    layout; the fence is waited, then D3D11 copies it into a `CreateSwapChainForComposition`
    swapchain (premultiplied alpha) on a DirectComposition visual over the HWND.
  - A D3D event query keeps the next frame from overwriting the texture mid-copy.
  - Opaque windows, and other platforms (premultiplied composite alpha where offered), keep
    the plain Vulkan swapchain.
  - If the interop is missing, the window falls back to opaque with a warning.

---

## 5. Renderer (WebGPU)

### Baseline
- **WebGPU through Dawn**, Google's native implementation. Dawn runs on Direct3D 12 (or 11) on
  Windows, Metal on macOS, and Vulkan on Linux; `ELG_GPU_BACKEND` picks one by hand. Immediates
  (WebGPU's push constants) carry per-draw parameters. Optional Dawn features: shared texture
  memory and fences (DXGI handles), for DirectComposition and zero-copy video.
- (Before October 2026: Vulkan 1.3 with dynamic rendering and synchronization2, through volk and
  VMA, and MoltenVK on macOS.)
- Use bindless descriptor indexing for textures, which allows one big draw for many images.

### Primitives
Everything the UI draws comes down to a small set of primitives:
1. **Rect**: fill (solid, linear, radial, or conic gradient), per-corner radii, border (per-side
   width and color), and box-shadow. Drawn as an SDF in the fragment shader, with analytic AA.
2. **Text run**: glyph quads sampled from the MSDF glyph atlas.
3. **Image**: with fit modes, radius clipping, and 9-slice.
4. **Path**: vector paths for shapes, `Path` views, and SVG. Rasterized **on the GPU**
   (decided; see "Vector paths" below).
5. **Clip / layer / effect**: rounded-rect clips, opacity groups, blur (backdrop-filter), and
   transforms.

### Batching strategy
- The display list is converted into instanced draws. One "UI quad" pipeline with a type tag
  handles rects, text, and images, so most frames are only a handful of draw calls.
- Offscreen passes happen only when needed: opacity groups with overlap, blur, and masks.

### Text rendering: MSDF (decided)
- Glyphs are stored as **MTSDF**: multi-channel SDF in RGB plus a true SDF in alpha.
  - One atlas entry serves every size, scale animation, rotation, and DPI. There's no
    re-rasterizing per size.
  - The alpha channel gives cheap text effects in the shader: outline, glow, soft shadow, and
    faux-bold (dilating the distance field).
- **Generation**: msdfgen runs on the worker pool, from FreeType outlines.
  - Common sets (ASCII/Latin for the default UI font) can be prebuilt at build time and embedded.
  - Generated glyphs are cached on disk per font hash, so cold starts skip generation.
- **Atlas**: an array texture with shelf packing, LRU eviction, and uploads through a staging
  ring buffer owned by the render thread.
- **Known weaknesses, with mitigations:**
  - *Small sizes (< ~12 px) look softer than hinted bitmaps.* Generate at a sufficient glyph em
    size (e.g. 48 px) with an adequate pixel range, snap baselines to pixels, and use
    screen-space-derivative AA in the shader. If body text still isn't crisp enough on low-DPI
    screens, add an optional hinted-bitmap path for small sizes later (**TODO**, measure first).
  - *Color glyphs* (emoji: COLRv1/CBDT/sbix) can't be MSDF. They go to a separate RGBA bitmap
    atlas, rasterized per size bucket.
  - *Very thin or complex glyphs* (CJK at small sizes) may show artifacts. Run msdfgen's error
    correction and test with real CJK fonts early.
- **Icons**: monochrome SVG icons (SF Symbols-style) also go through the MSDF pipeline. They
  scale and tint freely, cost as little as a glyph, and don't need path rendering.

### Vector paths: GPU (decided)
Arbitrary paths (`Path`, SVG, custom `Shape`s other than the analytic rect, capsule, and circle
SDFs) are rasterized on the GPU. **OPEN**: which technique?
- **Stencil-then-cover** (proposed first). Fill the path's triangle fan into the stencil buffer
  (non-zero/even-odd), then draw a cover quad with the fill shader. It's simple, robust, and works
  well in Vulkan. AA comes from MSAA on a path layer, or from analytic coverage at edges.
- **Compute rasterizer** (Vello-style: flatten → bin → coarse → fine in compute shaders). Best
  quality and scaling for path-heavy scenes, but a large project. It's the later upgrade path.
- **Avoid**: per-pixel curve evaluation (Slug-style) is patented.
- Flattening and path caches: a path's GPU buffers are cached by its identity and rebuilt only
  when it changes. Static icons go to MSDF instead (above).

### Frame pacing & power
- Redraw only on invalidation. Use `FIFO` present mode by default, plus damage rects
  (`VK_KHR_incremental_present`) where supported.
- Use triple buffering, with `MAILBOX` as optional for low latency.

### TODO
- [ ] HDR / wide gamut
- [ ] Color management (sRGB-correct blending vs "web-compatible" gamma blending)
- [ ] Device lost / swapchain recreation handling
- [ ] Software fallback? (lavapipe for CI screenshot tests)

---

## 6. State & Reactivity

This is the core of "stateful".

### Model: fine-grained signals (proposed)
Use a SolidJS-style model rather than a React-style virtual DOM:
- `Signal<T>`: a mutable value source.
- `Computed<T>`: a derived value. It is lazy, memoized, and tracks its dependencies automatically.
- `Effect`: a side effect that re-runs when its dependencies change.
- Bindings connect a signal straight to an element property, so no tree diffing is needed.
  Changing `count` updates exactly one text node.

```cpp
// Core primitives. Most app code uses State/Binding instead (see below).
auto count = elg::signal(0);
auto doubled = elg::computed([&] { return count() * 2; });

elg::effect([&] { log("count is {}", count()); });

count.set(5);           // effect re-runs, doubled is invalidated
count.update([](int& c) { ++c; });
```

### SwiftUI-facing names
The user-facing API uses SwiftUI vocabulary on top of the signal core:

| SwiftUI              | Elegance                          | Underlying                        |
|----------------------|-----------------------------------|-----------------------------------|
| `@State var x = 0`   | `auto x = State(0);`              | `Signal<int>` owned by the view   |
| `@Binding var x`     | `Binding<int> x`                  | get/set pair (from State or store)|
| `$x`                 | `x.binding()` (or implicit)       |                                   |
| `@Environment(\.k)`  | `Environment<K>()`                | Inherited context value (§8)      |
| `@Observable class`  | `Store<T>`                        | Per-field signals                 |

**Key difference from SwiftUI**: a view's body function runs **once** when the view is mounted.
It is not re-evaluated on every state change. Dynamic parts are passed as closures or bindings,
so updates are surgical and there's no body diffing. See "reactive arguments" in §9.

### Structural reactivity
- `If(cond, then_view, else_view)`: conditional subtrees.
- `ForEach(collection, key_fn, view_fn)`: keyed lists, diffed by key, with stable element reuse.
- `Switch(value).Case(...)`.

### Stores
- `Store<T>`: a reactive struct or collection with per-field tracking. This is a candidate for
  reflection once compilers ship it. On C++23 (§3.1), fields are declared with
  `ELG_STORE(T, field...)`.
- **OPEN**: Global app state pattern? Options: context/provider injection, explicit stores, or
  both.

### Scheduling
- Signal writes are batched within a frame. Effects flush before environment/layout.
- **OPEN**: Thread safety. Proposed: signals are UI-thread only, and `elg::post_to_ui(fn)`
  marshals updates from background threads. Async helpers include `Resource<T>`
  (loading/error/value), similar to Solid's `createResource` or React Query.

### Lifetime
- Ownership is tied to component scope: when a subtree unmounts, its effects and computeds are
  disposed automatically.
- **OPEN**: Handle-based (arena + generation indices) vs `shared_ptr`. Proposed: arenas, for
  cache locality and no ref-count churn.

---

## 7. Element Tree & Layout

### Element tree
- A retained tree of `Element` nodes stored in a contiguous arena (SoA where hot: layout boxes,
  paint props). This is an internal structure. App code never touches it and only sees `View`s.
- Each node holds a kind (stack, text, shape, image, …), layout props, paint/effect props,
  environment overrides, event handlers, and dirty flags.
- Built by **lowering** a view and its modifier chain on mount (§9.4).

### Layout: the SwiftUI model
Layout follows SwiftUI's negotiation rather than flexbox:

1. **The parent proposes** a size to the child. Each axis is optional:
   `nullopt` asks for the ideal size, `0` asks for the minimum, and `∞` asks for the maximum.
2. **The child chooses** its own size given the proposal. The parent must respect that choice.
3. **The parent places** the child within its bounds using alignment.

Containers and layout tools:
- `VStack` / `HStack` / `ZStack`, with `spacing` and `alignment`. Stacks give space to the least
  flexible children first, and `.layout_priority(n)` changes that order.
- `Spacer()` (flexible gap) and `Separator()` / `Separator(Axis::vertical, {.color, .thickness, .inset})`.
- `.frame(width, height, alignment)` and
  `.frame({.min_width, .ideal_width, .max_width = infinity, ...})`.
- `.padding(...)`, `.fixed_size()`, `.aspect_ratio(r, ContentMode::fit/fill)`, `.offset(x, y)`.
  `.offset` is visual only and doesn't affect layout, same as SwiftUI.
- `Grid` / `GridRow`, plus `LazyVGrid` / `LazyHGrid` with `GridItem::fixed/flexible/adaptive`.
- `LazyVStack` / `LazyHStack` / `List`: virtualized, so only visible rows are built.
- `ScrollView(axes)`, `ViewThatFits`, `GeometryReader` (escape hatch; use sparingly).
- Alignment guides: `.alignment_guide(HorizontalAlignment::leading, fn)` and custom alignment IDs.
- **Custom layouts**: implement the `Layout` interface:
  ```cpp
  struct FlowLayout : elg::Layout {
      Size size_that_fits(ProposedSize, Subviews, Cache&) const override;
      void place_subviews(Rect bounds, ProposedSize, Subviews, Cache&) const override;
  };
  // FlowLayout{}(child1, child2, ...)  -- used like a stack
  ```

Implementation notes:
- Within a priority, stacks give space to content before spacers: content splits what's left
  (minus the spacers' minimum lengths), and spacers split the rest. So
  `HStack { Text(...); Spacer() }` shows the text in full whenever it fits.
- Custom engine. Yoga and Taffy implement flexbox/CSS semantics and don't match this protocol.
  It's also a simpler model than flexbox, so writing our own is reasonable.
- Each node caches proposal → size. Invalidation propagates upward until it reaches a node whose
  size can't change (fixed frame, unchanged result).
- Units are logical points. DPI scaling happens at the renderer.
- **Text competes for space like any flexible view.** It can shrink to one line (with an
  ellipsis) and grow up to its full height, so next to a `Spacer` it gets an equal share and may
  truncate. As in SwiftUI, `.fixed_size(false, true)` makes it take its full height.

---

## 8. Styling: Modifiers, Environment & Styles

There are no stylesheets and no cascade. Appearance is declared in code with modifiers. Reuse
comes from three mechanisms: **environment values**, **custom modifiers**, and **styles**.

### Environment (inherited values)
Some modifiers don't style the view itself. Instead they set a value for the whole subtree,
which is the SwiftUI equivalent of CSS inheritance:
- `.font(...)`, `.foreground_style(...)`, `.tint(...)`, `.disabled(bool)`,
  `.color_scheme(...)`, `.line_spacing(...)`, `.button_style(...)`, `.environment(key, value)`.
- Views read these with `Environment<Key>()`, which is reactive. Changing a value re-resolves only
  the views that read that key.

### Theming
- A `Theme` is an environment value holding semantic tokens: colors (`Color::primary`,
  `Color::secondary`, `Color::accent`, `Color::background`, …), the font scale (`Font::title`,
  `Font::body`, `Font::caption`, …), spacing, and radii.
- Light and dark variants follow the system scheme by default. `.color_scheme(Dark)` overrides
  it for a subtree.
- **OPEN**: Load themes from a token file (JSON/TOML) with hot reload? That gives designers a way
  to iterate without recompiling.
- **Where the environment is read** (as built): a `ThemeColor`, or a closure taking
  `const EnvironmentValues&`, resolves where the view is placed. `Environment<Key>()` reads the
  environment where the code runs. That is the placement inside component bodies (structs with
  `body()`), ViewModifier bodies, and style `make_body`. A plain function returning a View runs
  before placement and sees its caller's environment.

### Styles (per-control appearance protocols)
Controls separate *behavior* from *appearance*, as SwiftUI does:
```cpp
struct PillButtonStyle : elg::ButtonStyle {
    View make_body(ButtonConfiguration cfg) const override {
        return std::move(cfg.label)
            .padding({.h = 16, .v = 8})
            .background([=] { return cfg.is_pressed() ? Color::accent.darker() : Color::accent; })
            .foreground_style(Color::white)
            .clip_shape(Capsule());
    }
};

VStack(Button("Save", save), Button("Cancel", cancel))
    .button_style(PillButtonStyle{});   // applies to every Button in the subtree
```
The same pattern applies to `ToggleStyle`, `TextFieldStyle`, `ProgressStyle`, `ListStyle`,
and others. Each built-in control ships with a default style plus a few alternates.

### Custom modifiers
```cpp
struct Card : elg::ViewModifier {
    View body(View content) const override {
        return std::move(content)
            .padding(16)
            .background(Color::background.elevated())
            .clip_shape(RoundedRectangle(12))
            .shadow({.radius = 8, .y = 2, .color = Color::black.opacity(0.15)});
    }
};

Text("Hello").modifier(Card{});
```

### Interaction states (replacing `:hover`, `:active`, `:focus`)
- `.on_hover(hovered)` writes into a `State<bool>`, and `.focused(is_focused)` binds focus.
- Reactive arguments (§9.3) then drive appearance:
  `.opacity([=] { return hovered() ? 0.8f : 1.f; })`.
- Controls expose their states through their style configuration (`cfg.is_pressed`,
  `cfg.is_hovered`, `cfg.is_focused`, `cfg.is_enabled`).

---

## 9. Authoring API

A SwiftUI-style C++ API. Views are values, and modifiers chain.

### 9.1 Example
```cpp
#include <elg/elegance.h>
using namespace elg;

View Counter() {
    auto count = State(0);

    return VStack({.spacing = 12},
        Text([=] { return std::format("Count: {}", count()); })
            .font(Font::system(32, Weight::bold))
            .foreground_style(Color::primary),

        HStack({.spacing = 8},
            Button("−", [=] { count -= 1; }),
            Button("+", [=] { count += 1; })
        )
    )
    .padding(24)
    .background(LinearGradient({Color::hex(0x6E8BFF), Color::hex(0xA36BFF)}, UnitPoint::top,
                               UnitPoint::bottom))
    .clip_shape(RoundedRectangle(16))
    .shadow({.radius = 12, .y = 4});
}

int main() {
    App app;
    app.window({.title = "Counter", .width = 400, .height = 300}, Counter);
    return app.run();
}
```

### 9.2 Core concepts
- **`View`** is an immutable, cheaply copyable, type-erased value: a `shared_ptr` to a
  description node. It describes UI and isn't the UI itself: nothing is created until it's
  mounted. Each modifier wraps the view in a new node, like SwiftUI's `ModifiedContent`, so
  modifiers are plain `const` methods. Views can be stored, reused, and captured freely. Every
  modifier method is sugar over `View::modifier()` (§9.4).
- **Option structs use `OptionalLength`**, not `std::optional<float>`. That type takes any number
  (`{.spacing = 12}`), whereas `std::optional<float>` triggers narrowing warnings at `/W4` for
  int literals.
- **Type erasure on purpose**: SwiftUI's `ModifiedContent<ModifiedContent<...>>` generic nesting
  would blow up C++ compile times and error messages. A body runs once (§6), so erasing types
  costs a small amount only at mount time.
- **Specific view types** like `Text`, `Image`, and shapes have their own modifiers
  (`Text::bold()`, `Text::line_limit()`, `Image::resizable()`, `Shape::fill()`,
  `Shape::stroke()`). These must come **before** generic modifiers, because a generic modifier
  returns a plain `View`. SwiftUI has the same rule.
- **Children**: stacks take children as variadic arguments, with an optional leading options
  struct: `VStack({.alignment = HAlign::leading, .spacing = 8}, a, b, c)`. Dynamic children come
  from `ForEach(...)` and `If(...)`, which are also views.
- **Components**: a function returning `View` (as above). Alternatively, a struct with
  `View body() const` when it takes props:
  ```cpp
  struct Avatar { std::string url; float size = 40;
      View body() const { return AsyncImage(url).frame(size, size).clip_shape(Circle()); } };
  // usage: Avatar{.url = u, .size = 64}
  ```
- **Naming**: snake_case (`corner_radius`, `foreground_style`, `clip_shape`), which matches the
  standard library and the existing code. **OPEN**: Offer camelCase aliases for SwiftUI muscle
  memory? Proposed: no. Having one spelling keeps docs and autocomplete clean.

### 9.3 Reactive arguments
Any modifier or view argument of type `T` also accepts:
- a plain `T` (static),
- a `State<T>`, `Binding<T>`, or `Signal<T>` (tracked), or
- a closure returning `T` (a tracked computed value).

```cpp
Text(name)                                          // State<std::string>, live-updating
    .foreground_style([=] { return error() ? Color::red : Color::primary; })
    .opacity(visible_opacity);                      // Signal<float>
```
This is how a body that runs once still produces fully dynamic UI. Each reactive argument becomes
a binding that updates exactly one property on one element.

### 9.4 Modifiers: architecture and catalog

#### Principle: every modifier is a type, and every dot-method is sugar
SwiftUI defines `.padding()` as an extension on `View` that just wraps a padding modifier:
`extension View { func padding(...) -> some View { modifier(_PaddingLayout(...)) } }`.
Elegance does the same:
- Each built-in method only constructs a **public modifier struct** and passes it to the single
  entry point, `View::modifier()`.
- The framework has **no private path**. Anything `.padding()` does, user code can do with
  `.modifier(PaddingModifier{...})`.
- User modifiers are built from the same interfaces as the built-in ones.

```cpp
// include/elg/view.h (sketch)
class ELG_API View {
public:
    // The one entry point. Built-in and user modifiers both go through here.
    template <std::derived_from<Modifier> M>
    View modifier(M m) && { return std::move(*this).push(AnyModifier(std::move(m))); }

    // Built-in "extensions": declared here, each a one-liner defined in the library.
    View padding(Prop<Insets> insets) &&;     // -> PaddingModifier
    View padding(Prop<float> all = 8) &&;     // -> PaddingModifier
    View frame(FrameSpec spec) &&;            // -> FrameModifier
    View background(Background bg) &&;        // -> BackgroundModifier
    View clip_shape(AnyShape shape) &&;       // -> ClipShapeModifier
    View font(Prop<Font> font) &&;            // -> EnvironmentModifier<FontKey>
    // ...

    // Function-style helpers: v.apply(card) == card(std::move(v))
    template <std::invocable<View&&> F>
    View apply(F&& f) && { return std::forward<F>(f)(std::move(*this)); }

private:
    View push(AnyModifier m) &&;
    detail::ViewRef node_;   // type-erased content + ordered modifier chain
};

// src/modifiers/layout.cpp
View View::padding(Prop<Insets> insets) && {
    return std::move(*this).modifier(PaddingModifier{std::move(insets)});
}
```

- **Header layout**: `view.h` only *declares* the sugar methods, which keeps the header that
  every view file includes light. That matters for compile time and hot reload speed. The
  modifier structs live in grouped public headers (`elg/modifiers/layout.h`, `effects.h`,
  `environment.h`, `events.h`, …), and the umbrella header includes them all.
- **Specific view types** (`Text`, `Image`, `Shape`) inherit every generic modifier from `View`.
  Their own modifiers (`Text::bold() && -> Text`) return the specific type, so you can keep
  chaining them until a generic modifier turns the value into a plain `View`.

#### Modifier protocols
`Modifier` is a closed set of base interfaces, one per pipeline stage, so the engine always knows
how to lower a modifier. Users can implement any of them.

| Protocol                | Hooks                                                      | Built-in implementations                                            |
|-------------------------|------------------------------------------------------------|---------------------------------------------------------------------|
| `ViewModifier`          | `View body(View content) const`                            | none; this is pure composition, and most user modifiers are this kind |
| `LayoutModifier`        | `size_that_fits(proposal, child)`, `place(bounds, child)`  | `PaddingModifier`, `FrameModifier`, `AspectRatioModifier`,          |
|                         |                                                            | `FixedSizeModifier`, `OffsetModifier`, `LayoutPriorityModifier`     |
| `EnvironmentModifier<K>`| value for key `K` in the subtree                           | `font`, `foreground_style`, `tint`, `disabled`, `button_style`, …   |
| `DecorationModifier`    | secondary view, alignment, z-order (behind/in front)       | `BackgroundModifier`, `OverlayModifier`, `BorderModifier`           |
| `DrawModifier`          | `draw(DrawContext&, Rect bounds)` before/after content     | `ShadowModifier`; also used for custom paint effects                |
| `EffectModifier`        | layer props: opacity, transform, clip, mask, filter, blend | `OpacityModifier`, `ClipShapeModifier`, `MaskModifier`,             |
|                         |                                                            | `BlurModifier`, `ScaleEffectModifier`, `RotationEffectModifier`     |
| `EventModifier`         | event handlers, gesture recognizers, focus                 | `TapGestureModifier`, `HoverModifier`, `FocusedModifier`,           |
|                         |                                                            | `KeyPressModifier`                                                  |
| `LifecycleModifier`     | mount/unmount hooks                                        | `OnAppearModifier`, `TaskModifier`, `OnChangeModifier`              |

#### Lowering and fusion
- At mount, `ViewModifier`s expand: each `body()` is called once and turns into primitive
  modifiers.
- **Order matters**, as in SwiftUI. `.padding(8).background(c)` paints behind the padding, while
  `.background(c).padding(8)` paints inside it. Lowering preserves that order.
- Primitive modifiers are identified by a **stable `ModifierKind` id**, not by `typeid` or an
  address, so hot reload doesn't break them (§9.6).
- **Fusion**: built-in kinds have fast paths that merge adjacent modifiers into one element. For
  example, `.padding().background().clip_shape().shadow()` becomes one node and one SDF quad,
  not four nested nodes. A user-defined primitive modifier is fully supported but acts as a
  fusion barrier: the result is correct, just not merged.
- **Dogfooding rule**: every built-in modifier must be expressible through the public
  protocols. Built-ins only add fusion fast paths on top. Tests enforce this by reimplementing a
  few built-ins (e.g. `aspect_ratio`, `border`) purely with the public API and comparing
  rendered output.

#### User extensions (`extension View` equivalent)
C++ has no extension methods, and C++26 adds no member injection or UFCS. Elegance gets real
dot-syntax extensions with an **extension header** instead. This is the same technique as
Eigen's `EIGEN_MATRIXBASE_PLUGIN` and Dear ImGui's `IM_VEC2_CLASS_EXTRA`: a user header is
`#include`d *inside* the class body of `elg::View`.

**1. Declare the extensions.** This file is included inside `class View { ... }`, so it holds
declarations only.
```cpp
// app/view_extensions.h
View card(float radius = 12) &&;
View badge(elg::Prop<int> count) &&;
View debug_border(elg::Color c = elg::Color::red) &&;
```

**2. Define them as normal code**, either in a header after `<elg/elegance.h>` (mark them
`inline`) or in any `.cpp` of the module:
```cpp
// app/view_extensions.cpp
#include <elg/elegance.h>
#include "card.h"

elg::View elg::View::card(float radius) && {
    return std::move(*this).modifier(app::Card{radius});
}
elg::View elg::View::badge(Prop<int> count) && {
    return std::move(*this).overlay(app::Badge(count), Alignment::top_trailing);
}
```

**3. Register the header per target** in CMake:
```cmake
elegance_view_extensions(my_app_views app/view_extensions.h)
# sets ELG_VIEW_EXTENSIONS="app/view_extensions.h" on every TU of the target
```

**4. Use them like built-ins**, on every view type, mixed freely with built-in modifiers:
```cpp
Text("Inbox").font(Font::headline).padding().card().badge(unread)
```

How it's wired in `view.h`:
```cpp
class ELG_API View {
public:
    // ... built-in modifiers ...
#ifdef ELG_VIEW_EXTENSIONS
#include ELG_VIEW_EXTENSIONS
#endif
private:
    detail::ViewRef node_;
};
// Guard: extensions may add non-virtual member functions only.
static_assert(sizeof(View) == sizeof(detail::ViewRef), "view extensions must not add data members");
static_assert(!std::is_polymorphic_v<View>, "view extensions must not add virtual functions");
```
**Export detail (Windows):** `View` and `Shape` export their members one by one
(`ELG_API` on each method), not the whole class. A `dllimport` class can't have members
defined outside the DLL, and extension definitions in app code are exactly that (MSVC error
C2491).

A matching `ELG_TEXT_EXTENSIONS` / `ELG_SHAPE_EXTENSIONS` hook covers type-specific extensions
(e.g. a custom `Text::code_style()`).

**Rules for extensions**
- **Sugar only**: functions only, with no data members and no virtuals (the guards above enforce
  this). Each body should come down to `.modifier()`, `.apply()`, or other modifiers, exactly
  like the built-ins.
- **One extension header per module**: it can `#include` several sub-headers, so third-party
  extension packs compose by being included from it.
- **Hot reload safe**: extension definitions live in the user module and are reloaded with it.
- **Standard caveat**: technically, the library's own TUs see a `View` without the extra members
  and the user's TUs see one with them, which is an ODR violation ("ill-formed, no diagnostic
  required"). In practice it is safe on MSVC, GCC, and Clang because the layout and vtable are
  identical and the added functions only exist in user code. Eigen and ImGui have relied on the
  same thing for years. The static_asserts guard the parts that would actually break.

**Without the extension header**, these forms always work:
```cpp
Text("Hi").modifier(Card{})                        // canonical; what SwiftUI desugars to
Text("Hi").apply(card)                             // function-style: View card(View v)
```

- **OPEN**: Also support a pipe syntax (`Text("Hi") | card()`)? Proposed: no, because it creates
  a third way to chain.
- **Future**: if C++ gets code injection (P3294) or UFCS, the extension header can become a
  first-class language feature with the same user-facing syntax.

#### Catalog (v1)

| Kind             | Modifiers                                                               | Lowers to                 |
|------------------|-------------------------------------------------------------------------|---------------------------|
| Layout           | `padding`, `frame`, `fixed_size`, `layout_priority`, `aspect_ratio`,    | layout props / wrapper    |
|                  | `alignment_guide`, `offset`, `position`                                 |                           |
| Decoration       | `background(View/Color/Gradient/Shape)`, `overlay(View, alignment)`,    | child layers sized to     |
|                  | `border(color, width)`, `corner_radius`                                 | the modified view         |
| Text/Environment | `font`, `font_weight`, `italic`, `foreground_style`, `tint`,            | environment values        |
|                  | `line_limit`, `multiline_text_alignment`, `line_spacing`, `kerning`     |                           |
| Clipping/Masking | `clipped()`, `clip_shape(Shape)`, `mask(View)`, `content_shape(Shape)`  | see below                 |
| Effects          | `opacity`, `shadow` (with `spread`), `inner_shadow`, `blur`,            | layer paint props;        |
|                  | `brightness`, `contrast`, `saturation`, `grayscale`, `hue_rotation`,    | offscreen only if needed  |
|                  | `sepia`, `color_invert`, `color_multiply`, `luminance_to_alpha`,        |                           |
|                  | `blend_mode`, `compositing_group`, `rotation_effect`, `scale_effect`,   |                           |
|                  | `background(Material::regular)` (blur behind)                           |                           |
| Interaction      | `on_tap_gesture`, `on_hover`, `gesture(...)`, `focusable`, `focused`,   | event handlers            |
|                  | `on_key_press`, `keyboard_shortcut`, `context_menu`, `help` (tooltip)   |                           |
| Lifecycle        | `on_appear`, `on_disappear`, `task(async fn)`, `on_change(value, fn)`   | effects tied to mount     |
| Presentation     | `sheet`, `popover`, `alert`, `confirmation_dialog`, `overlay` portals   | top-layer elements        |
| Accessibility    | `accessibility_label`, `accessibility_hint`, `accessibility_hidden`     | semantic tree             |
| Animation        | `animation(anim, value)`, `transition(...)`                             | see 9.5                   |

**Clipping and masking cost model** (the renderer is designed around this):
- `clipped()`: rect scissor, effectively free.
- `clip_shape(RoundedRectangle/Capsule/Circle)`: analytic SDF clip in the shader, with no
  offscreen pass.
- `clip_shape(Path)`: an offscreen layer masked by the path's cached coverage mask. It costs
  more than the analytic clips, but the mask is reused across frames.
- `mask(View)`: an arbitrary alpha mask. This needs an offscreen layer, so it's the most expensive
  option and the inspector should flag it.

**Effect cost model**:
- On a single shape (`[shadow*] shape [inner_shadow*]`), `opacity`, the color filters (computed on
  the CPU as a 4x5 matrix applied to the fill), `shadow` (with spread) and `inner_shadow` are
  analytic: no offscreen pass.
- Anything else gets one offscreen layer that carries every effect at once: blur, color matrix,
  blend mode, transform (rotation/scale; a uniform scale alone stays vector), inner shadow.
- `blur` and materials blur at a reduced resolution once sigma passes 12 px (downsample up to 8x).
- `blend_mode` and materials read the backdrop. The pass draws offscreen and is split there: what
  it drew so far is copied (and for materials, blurred) before the view composites over it.
  Inside a layer, the backdrop is that layer's content only.
- Rotation and scale don't move hit testing yet.

### 9.5 Shapes, color, fonts, animation
- **Shapes** are views: `Rectangle`, `RoundedRectangle(r, CornerStyle::continuous)` (squircle),
  `Capsule`, `Circle`, `Ellipse`, `Path`, and `UnevenRoundedRectangle`. Modify them with
  `.fill(style)`, `.stroke(style, width)`, and `.inset(by)`.
- **ShapeStyles**: `Color` (sRGB/P3, `hex`, `opacity`, semantic tokens), `LinearGradient`,
  `RadialGradient`, `EllipticalGradient`, `AngularGradient`, `ImagePaint`, and `Material`
  (frosted blur: `ultra_thin` … `ultra_thick`, with a dark-mode tint).
- **Fonts**: `Font::system(size, weight, design)`; semantic styles (`Font::large_title`,
  `title`, `headline`, `body`, `caption`) that scale with a global text-size setting; and
  `Font::custom("Inter", size)`. Text concatenation builds rich text:
  `Text("Hello ") + Text("world").bold()`.
- **Animation**:
  - **Implicit**: `.animation(Animation::spring(), value)` animates every animatable property
    that changes when `value` changes.
  - **Explicit**: `with_animation(Animation::ease_out(0.2), [&] { expanded = !expanded; })`
    animates everything that changes as a result of the state writes inside the closure.
  - **Insertion/removal**: `.transition(Transition::opacity.combined(Transition::slide))`.
  - **Later**: `matched_geometry_effect`, keyframe animations, `PhaseAnimator`.
  - Animatable properties are interpolated on the UI thread per frame. Only the affected paint
    or layout props are dirtied.

### 9.6 Hot reload (DLL-based)
Code is the only source of truth, since there's no markup (§1). Iteration speed comes from
rebuilding and reloading a DLL that contains the app's views.

#### Mechanism
```
app.exe / preview host  ──links──►  elegance.dll        (never reloaded)
        │
        └─ LoadLibrary ──►  app_views_<N>.dll   (user views; rebuilt + reloaded on save)
```
1. A file watcher sees a change and runs `cmake --build --target <app>_views`.
2. The new DLL (and its PDB) is copied to a unique name. A loaded DLL is locked on Windows, and
   `dlopen` caches by path on Linux/macOS.
3. Teardown: unmount the whole view tree, dispose every effect, closure, and task that module
   created, then join or cancel its in-flight async work.
4. Unload the old module, load the new one, call its exported `elg_module_entry()` (which
   returns the root view and the registered previews), and mount.

#### Framework rules (apply from M1; cheap now, painful to retrofit)
1. **Copy, never borrow.** The framework owns every string and value passed to it. It never
   keeps a `string_view` or pointer into user data, because string literals live in the
   module's memory.
2. **Stable keys.** Environment keys, style IDs, and modifier kinds use stable string/hash ids,
   never `typeid` or static addresses.
3. **Scoped ownership.** Every closure, effect, subscription, timer, and task belongs to a mount
   scope and can be torn down deterministically (§6 Lifetime).
4. **One framework instance.** Hot reload builds use the shared `elegance.dll` (dev builds force
   `ELG_BUILD_SHARED`) with the same compiler and CRT (`/MD`). A static framework would give
   each module its own globals.
5. **Light headers.** Reload latency is compile + link time. Public headers keep declarations
   only (§9.4), and builds use precompiled headers.

#### State across reloads
1. **Reset on reload**: what SwiftUI Previews does. This ships first.
2. **Model in host, views in module (recommended app pattern)**: app state lives in `Store`s
   owned by the exe and reaches views through the environment. Reloading swaps how things look
   and keeps all the data.
3. **Restore `State` by view identity**: values are saved by structural position and
   rehydrated if the type matches. This comes later, if ever, because it's complex and breaks
   for types defined inside the module.

#### Limits
- Changes to types shared with the host (model struct layout), to `main()`, or to dependencies
  need a full restart. The host should detect this (e.g. with a layout hash exported by the
  module) and say so.
- Statics in the module are re-initialized on each load.
- macOS `dlclose` may not actually unload (thread-locals, ObjC), so the host loads each module
  under a unique path and tolerates leaks in dev builds.

#### Extras
- `ELG_PREVIEW("Name") { return MyView(); }` registers previews, which the preview host lists
  and renders in isolation (SwiftUI Previews-style).
- Theme/token files (JSON/TOML; data only, not markup) reload without any rebuild.
- Live++ / Blink / jet-live (function patching) work alongside for anyone who has them. They're
  optional and not required.

**Estimated effort** (rough guess): about 500–1000 lines and 1–2 weeks for the reset-on-reload
host once views render. Restoring state by identity is another 1–2 weeks plus edge cases.

---

## 10. Events & Input

- DOM-like **capture → target → bubble** phases, with `stop_propagation` and `prevent_default`.
- Event types: pointer (unified mouse/touch/pen), wheel, keyboard, text input, IME composition,
  focus/blur, drag & drop, and resize.
- **Hit testing**: uses the layout boxes, transform-aware and clip-aware.
  `.allows_hit_testing(false)` opts a view out, and `.content_shape(Shape)` overrides the hit
  area.
- **Focus management**: tab order, focus scopes and traps (modals), and a focus ring shown only
  for keyboard focus. Bind focus with `.focused(binding)`.
  - Implemented (M4): one `FocusManager` per window. Keys go to the focused element's handler,
    then bubble out through its ancestors until one consumes them.
  - An unconsumed Tab / Shift+Tab moves focus through the focusable elements in tree order,
    wrapping around.
  - Pressing the pointer focuses the innermost focusable element under it, or clears focus.
  - The ring (in the tint) shows for Tab and programmatic focus, not for clicks, like the web's
    `:focus-visible`.
  - A destroyed element lets go of focus in its destructor, so removing a focused row is safe.
  - Controls are focusable: Enter activates a button on press; Space shows it pressed and
    activates on release.
  - `.focusable()`, `.focused(Binding<bool>)` (two-way), and `.on_key_press(Key, action)` /
    `.on_key_press([](const KeyEvent&) -> bool)`.
  - `KeyEvent` carries the layout-mapped `Key`, `KeyModifiers`, and `repeat`.
    `primary_modifier()` is Cmd on macOS and Ctrl elsewhere.
  - Still to do: focus scopes and traps; text input and IME (with TextField); keys while
    nothing is focused (window-level shortcuts).
- **Gestures**: tap, double-tap, long-press, drag, magnify, rotate, and kinetic scrolling.
  Attach them with `.gesture(...)`, compose them (`simultaneously`, `sequenced`, `exclusively`),
  and track in-progress state with `GestureState<T>`.
- **Shortcuts**: global and scoped keybindings, with a command registry.

---

## 11. Text & Editing

- Shaping with HarfBuzz, font fallback chains, system font discovery
  (DirectWrite / fontconfig / CoreText enumeration only), and emoji (COLRv1 / CBDT).
- Bidi, line breaking, ellipsis, max-lines, and rich text spans (inline styles and links).
- **Text input**: caret, selection, clipboard, undo/redo, IME (candidate window positioning),
  multi-line, and password mode.
  - Implemented (M4): single-line `TextField`. The pieces:
    - A `TextEditor` model keeps the text in sync with the binding, plus the caret and
      selection as byte offsets on HarfBuzz cluster boundaries, the IME composition, and undo.
    - A `TextEditorElement` draws the text where the `TextFieldStyle` puts it.
    - A focusable `TextFieldElement` around the style's body takes the input.
  - Editing:
    - Arrows; Ctrl+arrows (Alt on macOS) move by word; Home/End.
    - Shift extends the selection; Backspace/Delete, with word variants.
    - Ctrl/Cmd+A, C, X, V. Undo with Ctrl/Cmd+Z, where consecutive typing undoes together;
      redo with Ctrl+Y or Shift+Ctrl/Cmd+Z.
    - Enter runs `on_submit`.
  - Pointer: click places the caret (Shift+click extends), drag selects, double-click selects
    a word, triple-click selects everything. The I-beam cursor shows on hover.
  - Tabbing into a field selects its contents; clicking doesn't.
  - Text wider than the field scrolls to keep the caret in sight.
  - IME:
    - SDL text input is on while a text field has focus (the `FocusManager` toggles it).
    - The composition is drawn inline, underlined, at the caret.
    - The candidate window is positioned through `SDL_SetTextInputArea`.
  - Printable keys are consumed so outer `on_key_press` handlers don't see typing.
  - The caret blinks using timed wakes (`WindowServices::wake_at`), not continuous rendering.
  - Still to do: multi-line `TextEditor`, `SecureField`, selectable `Text`, a context menu
    (cut/copy/paste, §13.1), and grapheme boundaries from ICU/UAX #29 (HarfBuzz clusters
    approximate them).
- Selectable static text.
- **TODO**: A large-document strategy for code-editor-scale text (rope + virtualized lines)?
  Probably out of scope for v1.

---

## 12. Accessibility

- Expose a semantic tree (roles, names, states, actions) to the OS screen readers
  (UIA / AT-SPI / NSAccessibility).
- **Decided:** **AccessKit** through its C API (`accesskit-c`), rather than writing three backends.
  - CMake downloads the official prebuilt release (pinned version and SHA-256) and links it
    statically (`ELG_ACCESSIBILITY`, on by default). Building Elegance needs no Rust toolchain.
    MIT / Apache-2.0.
- Built-in components ship with correct roles. Keyboard navigation works everywhere, and
  `prefers-reduced-motion` and high-contrast settings are respected.

**How it works** (`src/view/accessibility.*`, `src/platform/accessibility_adapter.*`):
- **Element semantics:** each element describes itself (`virtual accessibility(AccessibilityInfo&)`):
  role, label, value, hint, placeholder, states (disabled, toggled, expanded, selected), numeric
  value and range, and supported actions. Elements without a role are transparent.
  - Text: a label. Icons and empty text are hidden.
  - Buttons combine their content's text into their name.
  - `Toggle` is a switch with its state. `MenuButton` and `Picker(.menu)` are pop-up buttons
    (combo boxes).
  - Sliders have their value, range, step, increment/decrement and set value.
  - `Progress` has its fraction (none while indeterminate). Text fields have their text (never a secure field's),
    placeholder and set value.
  - Images, video and scroll views have their roles. A `Separator` is a separator (AccessKit's
    splitter, as browsers map `<hr>`), not focusable; `.accessibility_hidden()` makes it
    decorative. `NumberInput` is a spin button with its
    value, range, step, increment/decrement and set value; its arrow buttons are named "Decrease"
    and "Increase".
- **Modifiers:** `.accessibility_label`, `_value`, `_hint` (reactive), `_hidden`, `_role`,
  `_element` (combine children into one element, labeled with their text) and `_action`. They
  apply to the element they wrap, or the nearest one inside with a role; around several children
  they form a group.
- **The tree is built on demand:** AccessKit's Windows subclassing adapter hooks the window's
  `WM_GETOBJECT` and stays dormant until a screen reader asks.
  - Once active, the whole tree is rebuilt from current values after each frame and sent, and
    AccessKit diffs it. No bookkeeping to keep in sync, and nothing at all while no screen
    reader is listening (idle CPU stays 0).
  - Node ids are per-element process-unique ids. Bounds are window points × scale. Focus is the
    focused element's node, or the nearest one around it.
- **Requests:** actions arrive on the screen reader's thread and are copied and posted to the UI
  thread (click, focus, increment/decrement, set value/number, scroll into view). Focus goes
  through the FocusManager; scroll into view through `reveal()` up the ancestors.
- **Verified** with Windows UI Automation (the API Narrator uses), from a script against the
  running sandbox:
  - Names, roles, values and toggle and range states read correctly.
  - Invoke, Toggle, RangeValue.SetValue, Value.SetValue on a text field, and SetFocus all act
    on the app.

---

## 13. Component Library (v1 candidates)

| Category   | Components                                                                 |
|------------|----------------------------------------------------------------------------|
| Layout     | VStack, HStack, ZStack, Spacer, Separator, Grid, LazyVStack/HStack,        |
|            | LazyVGrid/HGrid, ScrollView, ViewThatFits, GeometryReader, HSplitView      |
| Shapes     | Rectangle, RoundedRectangle, Capsule, Circle, Ellipse, Path                |
| Basic      | Text, Label (icon + text), Image, AsyncImage, Link, Badge                  |
| Input      | Button, TextField, SecureField, TextEditor, Toggle, Slider, NumberInput,   |
|            | Picker (menu/segmented/radio), DatePicker, ColorPicker                     |
| Data       | List (virtualized, sections, selection), Table, OutlineGroup (tree),       |
|            | DisclosureGroup                                                            |
| Overlay    | sheet, popover, alert, confirmation_dialog, Menu, context_menu, help       |
|            | (tooltip), Toast                                                           |
| Feedback   | Progress (linear/circular), Gauge, redacted (skeleton)                     |
| Navigation | NavigationStack (push/pop with transitions), NavigationSplitView, TabView  |

- Presentations (sheets, popovers, menus) render into a top layer so they escape clips.
  Anchored popups render in their own popup windows (§13.1).
- All controls take their appearance from the theme (environment) and a swappable `*Style`
  (§8). The default theme ships with light and dark variants.

### 13.1 Popups and context menus

Context menus share one **popup presentation layer** with everything else anchored to a view:
`Menu` buttons, `Picker` / `ComboBox` dropdowns, `popover`, and tooltips (`.help()`).

#### API
SwiftUI-shaped. Menu contents are ordinary views, so `If`, `ForEach`, and reactive arguments
work inside them:

```cpp
Text(file.name)
    .context_menu([=] {
        return Menu(
            MenuItem("Rename", rename).shortcut("F2"),
            MenuItem("Duplicate", duplicate),
            If([=] { return can_share(); }, MenuItem("Share", share)),
            Separator(),
            Submenu("Move to", ForEach(folders, key, [=](const Folder& f) { return MenuItem(f.name, move_to(f)); })),
            MenuItem("Delete", remove).role(ButtonRole::destructive).disabled([=] { return locked(); }));
    })
```

- The builder runs each time the menu opens, in a fresh `Scope` disposed on close. Items reflect
  current state, and nothing stays subscribed while the menu is closed.
- Item actions run after the menu has closed, so an action can open a dialog or another menu.
- Items: `MenuItem` (title, optional icon, shortcut hint, `disabled`, `checked`, `role`),
  `Separator`, `Submenu`, and `Section` (a titled group).

#### Rendering: drawn by Elegance, in a popup window (decided)

| Option | Pros | Cons |
|---|---|---|
| Native OS menus (`TrackPopupMenu`, `NSMenu`) | Exact platform feel | Unstyleable, no custom views, nothing native on Linux/Wayland, contradicts "Elegance draws everything" (§1) |
| Overlay drawn inside the window | Simplest; uses the top layer | Clipped to the window: breaks near edges and in small windows |
| **Elegance-drawn, in an SDL popup window** | Extends past the window, fully styleable, Wayland via `xdg_popup` | A small window with its own swapchain per popup |

- Popups use `SDL_CreatePopupWindow` (`SDL_WINDOW_POPUP_MENU` / `SDL_WINDOW_TOOLTIP`).
- To the rest of the framework a popup is just another window: its own element tree, event
  dispatcher, and frames through the multi-window renderer (§15).
- The app keeps one popup window ready and reuses it, so opening a menu doesn't wait for a
  swapchain to be created.
- **Embedding mode** (§4), which has no platform layer, falls back to an in-window overlay in the
  top layer.

#### Behavior
- **Opening:**
  - Right-click: opens on release on Windows, on press on macOS.
  - The Menu key, Shift+F10, Ctrl-click on macOS, and long-press on touch.
  - `context_menu` is an `EventModifier` on the right button, so the innermost context menu under
    the pointer wins, like taps (§10).
- **Positioning:** at the pointer, flipped or shifted to stay on the current display's usable
  area. Submenus open beside their item and flip at screen edges.
- **While open, the menu takes over input:**
  - Clicking outside, Esc, losing focus, or moving or resizing the parent window dismisses it.
  - The click that dismisses a menu is not passed through to the view underneath.
- **Keyboard:** Up/Down, Enter/Space, Left/Right into and out of submenus, Home/End, and
  type-to-select by first letters.
- **Submenus:** open on hover after a short delay. Moving diagonally toward an open submenu
  doesn't close it.
- **Accessibility:** `menu` / `menuitem` roles via AccessKit (§12). Focus returns to the previous
  view on close.
- **Styling:** a `MenuStyle` alongside `ButtonStyle` (§8, M3), built on theme tokens, with a
  default that looks at home on each platform.

#### macOS menu bar
The app menu bar on macOS is native (`NSMenu`), exposed separately as `App::menu_bar(...)`.
People expect the real system menu bar, and it isn't app-drawn there anyway. Windows and Linux
have no system menu bar, so an app menu bar there is an ordinary view.

#### Dependencies
Keyboard and focus (M4) for navigation, and M3's styles for `MenuStyle`. Scheduled for **M4**,
alongside `sheet` and `popover`.

---

### 13.2 Built-in icon library

Apps need icons everywhere: toolbars, buttons, menus, list rows, and empty states. Elegance ships
an existing open-source icon set rather than drawing its own. **No generated icons:** every glyph
comes verbatim from the upstream project, and the code table is generated mechanically from the
project's own metadata.

#### Candidates

| Set | License | Size | Styles | Notes |
|---|---|---|---|---|
| **Phosphor** (recommended) | MIT | ~1,500 icons | 6 weights: thin, light, regular, bold, fill, duotone | Ships TTF fonts per weight. The weights map onto `Weight`, so an icon can follow `.font_weight()` like SF Symbols. Neutral, modern look. |
| Lucide | ISC | ~1,500 icons | 1 (stroke) | Very popular on the web (Feather successor). Ships an icon font; stroke width only adjustable with vector paths. |
| Material Symbols | Apache-2.0 | ~3,000+ icons | Variable font: fill, weight, grade, optical size | Largest set. Needs variable-font axis support in the font loader. Distinctly "Google". |
| Fluent UI System Icons | MIT | ~2,000+ icons | regular, filled | Matches Windows 11. Less neutral on macOS/Linux. |
| Tabler / Heroicons / Bootstrap Icons | MIT | 300–5,000 | 1–2 | Fine alternatives; fewer weights. |

All of these licenses are compatible with MPL-2.0. The license file ships next to the font, and
`ELG_ICONS=OFF` drops the set for apps that don't want it.

#### API: `Image`, like SwiftUI's `Image(systemName:)`

Icons are images named by a dotted string, `"<set>.<icon-name>[.<variant>]"`:

```cpp
Image({.system_name = "lucide.person-standing"})        // set "lucide", icon "person-standing"
Image({.system_name = "phosphor.heart.fill"})           // a variant, SF Symbols style
Image({.system_name = "magnifying-glass"})              // no set prefix: the default set
Image("lucide.trash")                                   // shorthand for {.system_name = ...}

Image({.system_name = "lucide.gear"}).font(Font::title2)            // size follows the font
Image({.system_name = "phosphor.gear"}).font_weight(Weight::bold)   // weight -> icon weight
Image({.system_name = "lucide.trash"}).foreground_style(ThemeColor::danger)

Label("Search", {.system_name = "lucide.search"})       // icon + text, spaced and aligned
Button(Label("Delete", {.system_name = "lucide.trash-2"}), remove)
MenuItem("Copy", copy, {.image = {.system_name = "lucide.copy"}, .shortcut = "Ctrl+C"})
```

- `Image(ImageSource)` is one view for every kind of image. `ImageSource` is an options struct,
  so later sources join it as new fields: `.file = "logo.png"`, `.resource = ...`, `.pixels = ...`
  (raster images; see below). Exactly one field is set.
- **Name resolution:**
  - The text before the first dot is the set if it names a registered set (`lucide`,
    `phosphor`, ...). Otherwise the whole string is looked up in the default set.
  - Icon names are kept verbatim from upstream, e.g. Lucide's kebab-case `person-standing`, so
    names can be copied straight from the set's website.
  - A trailing segment naming a variant the set has (`.fill`, `.duotone`, `.bold`, ...) selects
    it, as SF Symbols' `heart.fill` does. Otherwise the icon's weight follows the font weight
    (sets that have weights).
- **Unknown names** throw `elg::UsageError` when the view is built, naming the closest matches
  (`no icon "lucide.persn-standing"; did you mean "lucide.person-standing"?`).
- **Optional typed constants** for autocompletion and compile-time checking: the generator also
  emits `Icons::lucide::person_standing` etc., each just the name string, so
  `Image({.system_name = Icons::lucide::person_standing})` works too.
- An icon `Image` acts like a glyph of text:
  - It takes the environment font size and foreground color.
  - It sits on the text baseline in `HStack`s and `Label`s.
  - `.resizable()` + `.frame(...)` scale it to a box instead.
- **Icon sets are a registry**; see *Adding an icon set* below.
- **Raster images (later, same view):** `Image({.file = "photo.png"})` with `.resizable()`,
  `.aspect_ratio(fit/fill)`, and `.interpolation(...)`. That needs an image decoder
  (e.g. wuffs or stb_image) and texture uploads through the atlas path.

#### Adding an icon set (extensibility)

Adding a set should take one line and no code: point Elegance at the files the set already ships.
The built-in sets are registered through this same public API (dogfooding), so it gets exercised
for real.

```cpp
// Most sets: an icon font plus the name -> codepoint map it ships with (format auto-detected).
register_icon_set("brand", {.font = "brand-icons.ttf", .names = "brand-icons.json"});

// Fonts whose glyphs are already named after the icons (common): the font alone is enough.
register_icon_set("brand", {.font = "brand-icons.ttf"});

// Variants as extra fonts ("brand.heart.fill"), and weights for .font_weight().
register_icon_set("brand", {.font = "brand-regular.ttf",
                            .names = "brand.json",
                            .variants = {{"fill", "brand-fill.ttf"}},
                            .weights = {{Weight::bold, "brand-bold.ttf"}}});

// From memory (embedded in the app), e.g. with the CMake helper below.
register_icon_set("brand", {.font_data = brand_ttf, .names_data = brand_json});

set_default_icon_set("brand"); // for names without a set prefix
```

- **Name maps read as shipped**, so nothing needs converting:
  - JSON objects `{"name": "e900"}` / `{"name": 59648}`.
  - Material-style `.codepoints` text (`name e900` per line).
  - CSS from icon webfonts (`.icon-name:before { content: "\e900"; }`), with a configurable
    class prefix.
  - IcoMoon `selection.json` and Fontello `config.json`.
  - Without a map, the font's own glyph names (the `post` table / CFF names).
- **CMake helper** to embed a set into an app, so there are no loose files at runtime:
  `elegance_icon_set(my_app NAME brand FONT icons/brand.ttf NAMES icons/brand.json)`. It
  generates the registration call and, optionally, `Icons::brand::...` constants.
- **SVG sets (once GPU paths exist):** `register_icon_set("brand", {.svg_directory = "icons/"})`,
  where each file name is an icon name and `heart-fill.svg` style suffixes can map to variants.
  That covers sets that ship only SVGs.
- **Packaging:**
  - Each built-in set is a separate, optional component (`ELG_ICON_SETS="lucide;phosphor"`),
    with its license file installed alongside.
  - Third parties can publish a set as a tiny vcpkg port or CMake package that calls
    `elegance_icon_set`.
- **Errors:**
  - Registering a name twice is a `UsageError` (unless `{.replace = true}`).
  - A missing file or unreadable map throws `ResourceError` with the path and what was expected.

#### Implementation

1. **Now, through the existing text pipeline.**
   - The set's TTF fonts render through FreeType, MSDF and the glyph atlas, like any text. That
     means crisp at every size, GPU-cheap, and nothing new in the renderer.
   - The fonts are embedded in the library binary (registered from memory), so apps carry no
     loose files.
   - A small script turns the upstream codepoint metadata into `Icons::`. The script and the pinned
     upstream version live in `tools/`, and the generated header is committed.
2. **Later, with GPU vector paths (§5).** Optionally switch to the set's SVG sources, for
   arbitrary stroke widths, multicolor and duotone layers, and animatable icons.
3. **Built-in controls use it.** These replace today's Segoe MDL2 glyphs and fallbacks
   (`view/icons.h`):
   - check marks: checkbox, menu checks, picker
   - chevrons: menu buttons, submenus
   - the close glyph for tooltips and tabs

   The result looks the same on every platform. Window caption buttons may keep the native glyphs
   on Windows.

#### Open questions
- **Which sets ship built in**, and which is the default for unprefixed names. Lucide (as in the
  examples) and/or Phosphor (for its weights). The registry supports several either way.
- Size of the embedded fonts. Phosphor at ~6 weights × ~150–250 KB each, about 1–1.5 MB. Ship
  regular + bold + fill by default, and the rest opt-in?

---

### 13.3 Media: images, audio, and video

Elegance exists so desktop apps don't need a browser. What people reach for a browser for
includes pictures, sound, and video, so these are built in rather than left to each app.
Today `Image` only draws icons, and nothing plays sound or video.

**Principles**
- **Platform decoders first.** Use the OS media stack: Media Foundation on Windows,
  AVFoundation on macOS, and GStreamer on Linux. This gives hardware decoding, the codecs the
  OS already licenses (H.264, HEVC, AAC, and AV1 where installed), and audio/video sync for
  free. An FFmpeg backend can come later as an option. Bundling it by default would bring a
  large binary, LGPL linking care, and codec patent questions.
- **Frames stay on the GPU.** Decoded video frames land in a GPU texture and are drawn like any
  other quad, with no CPU copy where the platform allows it. On Windows the Media Foundation
  engine renders into a D3D11 texture shared with Vulkan, the same interop that DirectComposition
  presentation uses. macOS uses IOSurface → MoltenVK, and Linux uses DMA-BUF import.
- **Reactive players.** A player's time, duration, state, and volume are signals, so a
  scrubber is just `Slider(player.time_binding(), ...)`, and views update without callbacks.
- **The UI thread never blocks.** Decoding, file and network I/O, and audio mixing run on their
  own threads. Results reach the UI thread like everything else, through signals and
  `post_ui_task`.

**1. Raster images.** This is the foundation for the rest: the texture path video reuses.
```cpp
Image({.file = "assets/photo.jpg"}).resizable().aspect_ratio(ContentMode::fill).frame(200, 120).clip_shape(RoundedRectangle(12))
Image({.data = png_bytes})                                  // from memory (embedded with elg_embed_file)
AsyncImage("https://example.com/avatar.png", [](AsyncImagePhase phase) { ... })   // placeholder / loaded / failed
```
- Decoding: WIC on Windows, ImageIO on macOS, and stb_image/libspng on Linux. Formats are PNG,
  JPEG, GIF (first frame, then animated), WebP and AVIF where the OS supports them, and SVG
  through the path renderer.
- Textures are uploaded on the render thread and cached by content hash, with mipmaps for
  downscaling. Decoded sizes follow the drawn size (no 4000 px decode for a 64 pt thumbnail).
- `ImageCommand` in the display list draws a textured quad with corner radii, opacity, and the
  clip stack, through the same ubershader as layers.
- `AsyncImage` loads over HTTP(S) through the platform: WinHTTP, NSURLSession, or libcurl. It
  has a memory cache plus an optional disk cache.

**2. Audio**
```cpp
const Sound click = Sound::load("assets/click.wav");    // short effects: decoded once, mixed, overlapping plays
click.play({.volume = 0.6f});
const AudioPlayer music("assets/theme.mp3");             // long files and streams: decoded as they play
music.play();  music.seek(30s);  music.volume = 0.5f;
Slider(music.time_binding(), {.max = music.duration()})
```
- Backend: **miniaudio** (public domain / MIT-0, single file, in vcpkg). It covers device
  output on every platform (WASAPI, Core Audio, PulseAudio/ALSA), mixing, resampling, and
  WAV/FLAC/MP3 decoding, plus Vorbis through stb_vorbis. AAC and other formats decode through
  the platform decoder when present.
- The audio callback thread never allocates or locks. Commands go through a lock-free queue.
- Device changes (headphones plugged in) and the system's default device are followed
  automatically.
- Later: microphone capture (`AudioRecorder`), with the platform's permission prompts.

**3. Video**
```cpp
const VideoPlayer player("assets/intro.mp4");           // also https:// URLs (progressive and HLS where the OS supports it)
VideoView(player).aspect_ratio(ContentMode::fit).clip_shape(RoundedRectangle(16))
VideoControls(player)                                   // built-in play/pause, scrubber, time, volume, fullscreen
player.play();  player.is_playing();  player.time();    // signals
```
- Windows: `IMFMediaEngine` handles demuxing, hardware decoding, audio output, and A/V sync.
  Each display frame, `TransferVideoFrame` writes into a shared D3D11 texture that Vulkan
  samples.
- macOS: `AVPlayer` + `AVPlayerItemVideoOutput` → `CVPixelBuffer` (IOSurface).
- Linux: a GStreamer `playbin` with an `appsink` (DMA-BUF when available).
- Frame pacing: the render thread asks the player for the frame due at the next vsync, so video
  never runs the UI tree's layout. Only the video quad's texture changes, and windows playing
  video redraw at the video's rate.
- Later: Vulkan Video decode as a backend independent of the OS, and camera capture
  (`CameraView`).

**3a. The FFmpeg backend (optional).** The OS media stack stays the default: it costs nothing to
ship, decodes in hardware, and the codec licenses are the OS vendor's. Only HEVC needs a paid
Store extension on Windows, and that falls on the user's machine. FFmpeg is the alternative
when an app needs the same formats everywhere, or containers and codecs the OS lacks.
- **Opt-in:** `ELG_VIDEO_FFMPEG=ON` (a vcpkg feature). `VideoPlayer({.source = ..., .backend =
  VideoBackend::ffmpeg})` picks it per player. `VideoBackend::automatic` uses the OS first and
  falls back to FFmpeg for what the OS can't open.
- **Licensing by default:** FFmpeg is linked dynamically and built LGPL-only (no GPL parts,
  `--disable-gpl --disable-nonfree`). The default decoders are royalty-free:
  - AV1 (dav1d), VP8/VP9 (libavcodec's own decoders).
  - Opus, Vorbis, FLAC, PCM.
  - Containers: MKV/WebM, MP4/MOV and Ogg.
- **Patent-encumbered codecs** (H.264, HEVC, AAC, AC-3...) are enabled only with
  `ELG_VIDEO_FFMPEG_PATENTED=ON`. When an app ships them, the patent royalties in the countries
  it ships to are its author's responsibility. The docs and the CMake option say so plainly
  (this design doesn't give legal advice).
- **Architecture:** a second `VideoSource` implementation behind the same `VideoPlayer`,
  `VideoView` and `VideoControls`. The renderer and views don't change.
  - **Demux** (libavformat): files, and HTTP(S)/HLS through FFmpeg's protocols (or our HTTP
    layer).
  - **Decode** on a decoder thread: hardware first, software as the fallback. FFmpeg's Vulkan
    hwaccel decodes into `VkImage`s on our device, so frames are sampled with no copy; the
    multi-planar NV12/P010 formats use a YCbCr sampler conversion. D3D11VA → the existing shared
    texture path is the Windows fallback; software frames upload into a ring of textures.
  - **Audio:** decoded and resampled (libswresample) into a miniaudio data source on the
    existing engine.
  - **A/V sync:** the audio clock is the master. Video frames are queued with their
    timestamps, and the render thread shows the one due at the next vsync, dropping late
    frames.
  - **Seeking:** to the keyframe before the target, then decode forward. The scrubber shows
    keyframes while dragging and the exact frame on release.
- **Extras this enables:** subtitle tracks (text and bitmap), audio track selection,
  thumbnails (`VideoPlayer::thumbnail(at:)` for scrubber previews), and frame-accurate stepping.
- **Not planned:** encoding and recording through FFmpeg. Capture uses the platform APIs, and
  encoding raises the same licensing questions without a default that's both royalty-free and
  widely playable.


**Order:** raster images → audio → video → AsyncImage and networking → capture (mic, camera).

### 13.4 Desktop integration

What apps otherwise reach for Electron's APIs for, on the platform's own facilities (SDL3 where
it covers them).
- **File dialogs:** `choose_files({.filters, .directory, .multiple})`, `choose_save_file({.filters,
  .directory, .name})`, `choose_folders`. Native dialogs (SDL3: the Windows file picker), over
  the focused window. Results reach the UI thread asynchronously.
- **Drag and drop in:** `.on_drop({.files, .text, .targeted})` on any view.
  - The innermost view under the pointer that takes that kind of thing gets it, with the
    location in its own coordinates. `targeted` drives a highlight while a drag hovers.
  - SDL3's drop events (begin, position, file, text, complete). The targeted view is held by
    path, so tree changes mid-drag are safe.
  - Dragging out of the app is a follow-up.
- **Notifications:** `notify({.title, .body, .on_click})`.
  - Windows: a shell notification icon (a message-only window and `Shell_NotifyIcon` with
    `NIF_INFO`), shown as a toast with the app's icon and kept in the notification center.
    Clicks come back through `NIN_BALLOONUSERCLICK`.
  - Proper WinRT toasts (actions, images) need an AppUserModelID and a Start-menu shortcut, so
    they're a follow-up for packaged apps.
- **Tray icon:** `TrayIcon({.icon, .tooltip, .menu})` (SDL3 tray): buttons, check boxes, separators
  and submenus. Shown while any copy lives; `set_tooltip` and `set_menu`. The icon comes from an
  `ImageSource`, else a plain dot.

### 13.5 Split views and navigation

The structure of most desktop apps (`<elg/navigation.h>`, example `navigation`).

#### Split views
`HSplitView(a, b, ...)` and `VSplitView(...)`: panes with a draggable divider between each two.
- **Pane limits come from the pane's own layout**, as in SwiftUI. There's no split-specific
  modifier:
  - a pane's minimum is what it takes when offered 0;
  - its maximum is what it takes when offered infinity;
  - it starts at its ideal size.
  So `.frame({.min_width = 180, .ideal_width = 240, .max_width = 400})` bounds a pane. Content
  that doesn't grow (a bare `Text`) can't be dragged wider than it is.
- **Preferred vs. actual sizes:** each pane keeps a preferred size: its ideal when it appears,
  then wherever a drag left it. Every layout starts from the preferred sizes.
  - The difference from the available length goes first to the pane with the highest
    `layout_priority` (ties: the last pane), then to the others from the last back, each within
    its limits.
  - A sidebar squeezed by a small window gets its width back when the window grows. Storing the
    squeezed size lost it, which showed up in the first test.
- **Dividers:** a line taking space between the panes, 1 pt in the outline color by default.
  - It can be grabbed 4 pt either side. Hover shows the resize cursor at once, and a 3 pt bar in
    the tint while hovered or dragged.
  - `HSplitView({.divider, .active_divider, .thickness, .active_thickness}, panes...)` changes
    them; `.thickness = 0` leaves no line or gap.
  - A drag moves only the two panes beside it, each within its limits.
- **Dynamic panes:** a pane can be an `If` (a collapsible sidebar). Panes are flattened like a
  layout's children; a pane that goes loses its preferred size, and one that animates out is
  painted where it was.
- **Clipping:** each pane is clipped to its share, so content larger than its pane doesn't run
  into the next.

`NavigationSplitView(sidebar, detail)` and `(sidebar, content, detail)` compose an `HSplitView`
of framed columns, with nothing it doesn't let you change. By default:
- the sidebar column is 180–360 pt (ideal 240) on the surface color (`sidebar_width`,
  `sidebar_background`);
- the three-column form adds a content column of 220–480 pt (`content_width`);
- the detail is at least 200 pt and takes size changes (`detail_width`, `layout_priority(1)`);
- the dividers are the split view's (`dividers`).

`sidebar_visible` (a `Binding<bool>`) puts the sidebar in an `If` with `sidebar_transition`
(sliding to the leading edge by default). Toggled inside `with_animation`, the other columns'
frames ease to their new widths.

#### NavigationStack
`NavigationStack(path, root, destination, {.root_title, .title, .transition, .animation,
.back_gestures})`.
- **Driven by a path** (`State<std::vector<Route>>` or a `Binding`), like SwiftUI's
  `NavigationStack(path:)`. Pushing and popping are edits to the path from anywhere, and a deep
  link or restored session is just a path.
  - `destination` builds a page from a route. Routes are compared with `==`: the longest
    unchanged prefix of pages is kept, and the rest are rebuilt.
  - A path present at mount (a deep link) shows at once, without animating.
- **Pages under the top one stay alive** until removed from the path, each in its own `Scope`.
  Scroll positions and state survive going back.
  - A covered page has a `.focusable(false)` handler, so Tab and clicks skip it.
  - It's `hidden` to screen readers, and hit testing and painting skip it.
  - It's still visited as a child, so the event dispatcher sees the pointer leave its controls,
    rather than leaving them stuck hovered.
  - If focus was in the page that got covered, it moves to the first control of the new page
    when the ring was showing (keyboard use), else it's dropped.
- **Transitions** (`PageTransition`):
  - `slide` (default): a push slides the new page in from the trailing edge, over the old one.
    The old page moves 30% of the way to the leading edge and dims slightly. The page on top gets
    the background color and a soft shadow while moving.
  - `fade`: the pages cross-fade. `none`: the top page changes at once.
  - A pop reverses it, and the popped page is kept until it has gone. The whole stack is
    clipped, so pages don't slide over a sidebar.
  - The animation is the transaction's when there is one (`with_animation`), else the
    `animation` option (`Animation::smooth(0.35)`; nullopt: at once).
- **Back gestures** (`back_gestures`, on by default): Alt+Left while focus is inside the stack,
  and a two-finger swipe right, which always slides.
- **Presentation is a style** (`.navigation_stack_style(...)`), so nothing iOS-like is forced.
  `NavigationStackConfiguration` has the pages, the titles (root first), `can_go_back` and
  `back`.
  - `PlainNavigationStackStyle` (default): just the pages.
  - `BarNavigationStackStyle{{.height, .background, .title_font, .back_color,
    .shows_divider}}`: the top page's title, and from the second page on, a back button (caret +
    the title of the page underneath).
  - `root_title` is a `Prop`, so it can follow state (the selected folder).
  - There are no toolbar items or per-page title modifiers yet. Those need a way for a page to
    pass values up to its container (SwiftUI's preferences), which Elegance doesn't have.

#### Not done yet
- Keyboard resizing of dividers, and a splitter role for screen readers. Dividers are hidden
  from accessibility for now.
- Persisting pane sizes (a binding to the preferred sizes).
- Double-clicking a divider to collapse a pane.
- The mouse's back button: `MouseButton::other` doesn't say which button it is.
- Toolbar items in the navigation bar, and `navigation_title` set by the page itself. Both need
  preferences.
- `TabView`.

## 14. Extensibility

- **Custom components**: functions or structs that return a `View`. Also custom
  `ViewModifier`s, `*Style`s, and `Layout`s (§7, §8).
- **Custom drawing**: a `Canvas` element with an immediate-mode 2D API, plus raw Vulkan
  callbacks for embedding 3D viewports.
- **No scripting layer** (decided; see §1 Non-Goals). Hot-swappable logic comes from DLL reload.

---

## 15. Threading Model

Separate render thread from day one (decided).

- **UI thread = main thread** (an SDL requirement): event pumping, input, reactivity,
  environment resolution, layout, animation ticking, and display list building.
- **Render thread**: owns the `VkDevice` queues, swapchain, and every GPU resource. It consumes
  frame packets and records and submits command buffers. A GPU or vsync wait never blocks the
  UI thread.
- **Worker pool**: image decoding, MSDF glyph generation, path flattening, text shaping for large
  blocks, and file I/O.

### Frame packets
- The UI thread produces an immutable **frame packet** per frame. It contains the display list
  (draw commands referencing resources by handle), resource requests (new or evicted glyphs,
  images to upload, paths to (re)build), and per-window swapchain state (size, scale).
- Packets go through a **coalescing mailbox** (`render/frame_mailbox.h`). A new frame for a
  window replaces that window's pending frame, so a slow GPU never builds up a backlog.
  Window add and remove commands are queued in order and run before frames.
- Every post returns a sequence number. The UI thread can wait on it (with a timeout) when a
  frame must be on screen first: the first frame before showing a window, and each frame
  during live resize.
- **Resource handles**: the UI thread allocates handles (generation-indexed IDs) right away. The
  render thread creates the backing GPU objects. A draw that references a not-yet-ready resource
  draws a placeholder or skips, and the UI never blocks waiting for an upload.
- **Resize**: the render thread recreates the swapchain. During live resize (§4) the UI thread
  sends new packets from the event watch callback.
  - If the last presented image doesn't match the window's size, the compositor stretches it.
    Keeping latency low hides that:
    - Recreation waits only on the window's own frames, not the whole device.
    - The old swapchain is retired: its views and semaphores are destroyed once a later
      submission completes, because a pending present may still wait on them.
    - Swapchains have two images, so at most one frame is queued.
    - After each live-resize frame the UI thread waits for the compositor (`DwmFlush`), so the
      new frame is on screen before the next resize step.
  - On Windows, every window now presents through DirectComposition (`gpu/composition_win32`),
    so that path is no longer just for transparent windows:
    - A composition swapchain is shown at its own pixel size, never stretched.
    - A late frame stays anchored top-left over a background layer in the window's clear color:
      a 1×1 surface scaled to cover the window.
    - Opaque windows use `DXGI_ALPHA_MODE_IGNORE`.
    - Frames are paced by a frame-latency waitable swapchain (latency 1) instead of a blocking
      present. The present uses sync interval 1: with 0 the waitable is signaled as soon as the
      present is queued, and an animating window rendered ~900 fps on a 75 Hz display (~50% GPU).
    - `ELG_PRESENT=surface` restores WebGPU surfaces for opaque windows.
    - `ELG_PRESENT=swapchain`, or RenderDoc being attached (`renderdoc.dll` loaded), puts every
      window on a WebGPU surface and creates no Direct3D objects of its own. RenderDoc can't capture
      DirectComposition or D3D11 texture sharing; the system's composition code crashed on its
      wrapped D3D11 device. Transparent windows render opaque then (logged once).
  - What triggers a live-resize frame: SDL's live-resize timer is a `WM_TIMER`, which Windows
    delivers only when no other message waits, so it fired irregularly during fast drags (one
    window got 16 frames in a drag where another got 55, with 100–200 px jumps). Frames are now
    also drawn on every pixel-size change while the thread is in the OS move/size loop
    (`GUI_INMOVESIZE`). The timer still covers animations while the drag pauses.
  - Lazy stacks keep measured sizes by item position, with a running total (in `double`), so
    measuring is O(1) and arranging does map lookups only for visible rows. The 10,000-row
    List's frame went from ~16 ms to ~1.6 ms in debug. Summing in `float` drifted by a whole row
    over 10,000 items.
  - Follow-ups:
    - Replace the CPU waits around the D3D11 copy with shared fences (the end-access fences
      Dawn's shared texture memory returns, waited on by D3D11).
- **Embedding mode** (§4) runs without the render thread: the host calls `elg::render(cmd)` on
  its own render thread with the latest packet.
- **Rule**: the UI thread never touches GPU objects (it hands the render thread each window's native
  handles; the render thread creates the surfaces), and the render thread never touches the
  element tree or signals.

---

## 16. Developer Experience

- **Hot reload / previews**: see §9.6.
- **Inspector** (first version done, see M5): toggled with F12, in its own window, with the view tree, each view's modifier
  chain (and what it fused into), environment values, layout proposals and sizes, the signal
  dependency graph, offscreen-pass warnings (`mask`, `blur`), and per-frame timings.
- **Profiling**: Tracy integration behind a compile flag.
- **Testing**: a headless mode (offscreen render target) for screenshot and golden-image tests,
  plus a synthetic input API for UI tests.

---

## 17. Performance Budgets (initial targets)

| Metric                                  | Target            |
|-----------------------------------------|-------------------|
| Cold start to first frame               | < 100 ms          |
| Idle CPU / GPU                          | ~0% (no redraw)   |
| Hover state change, 1k-element UI       | < 0.5 ms CPU      |
| Full relayout, 1k elements              | < 2 ms CPU        |
| Draw calls, typical frame               | < 20              |
| Scroll a 100k-row virtual list          | 60+ fps steady    |
| Memory, hello world                     | < 30 MB           |

---

## 18. Roadmap

**M0: Skeleton**
- [x] CMake structure: `include/elg/` (public API), `src/` (library), `apps/sandbox/` (test app)
- [x] `shaders/` (GLSL compiled offline by `cmake/ElgShaders.cmake`, embedded as SPIR-V)
- [x] `tests/` (Catch2 via the `tests` vcpkg feature, enabled in the presets)
- [ ] Examples (the elegance-examples repository)
- [x] SDL3 window + Vulkan init (volk, VMA) + swapchain + clear color
- [x] Render thread + frame mailbox (§15); live resize via SDL's expose event watch
  (confirmed on Windows: no freezing or stretching while dragging a window edge)
- [x] SDF rounded rect pipeline: fill, per-corner radii, border, analytic AA, one instanced draw
- [x] Error policy plumbing (§3.2): `elg::Error` types, `ELG_ASSERT`, crash reports,
  render-thread errors rethrown from `App::run()`; validation errors crash in debug builds
- Temporary: `elg::Canvas` + `DrawFunction` let the sandbox draw until Views land in M1.

**M1: Static UI**
- [x] `View` type + `View::modifier()` + modifier protocols (view, layout, decoration) +
  lowering to the element tree
- [x] User extensions via `elegance_view_extensions()` (the sandbox's `.card()`)
- [x] Follow the hot-reload framework rules from day one (§9.6)
- [x] Layout protocol + custom `Layout`: VStack/HStack/ZStack, Spacer, frame (fixed and
  flexible), padding, fixed_size, layout_priority, offset
- [x] Shapes (Rectangle, RoundedRectangle, Capsule, Circle; fill/stroke/stroke_border) +
  `background` / `overlay` / `border`; `Canvas` view (`GraphicsContext`)
- [x] Effects (`EffectModifier` protocol): `clipped`, `clip_shape`, `corner_radius`, `opacity`
  (group), `shadow`
  - Clips: a per-instance rounded-rect SDF in the shader (no extra pass). Nested rounded clips
    intersect exactly for rects; the corners use the innermost clip (approximation).
  - Fast path for a single shape: alpha multiply, plus an analytic blurred rounded-rect shadow.
  - General path: an offscreen layer (RGBA8 sRGB, pooled per frame in flight). Its shadow is a
    two-pass separable Gaussian of the layer's alpha (R8). Layers render innermost-first, before
    the window pass.
- [x] Gradients: `LinearGradient` / `RadialGradient` with any number of stops (per-frame storage
  buffer), interpolated in sRGB with premultiplied alpha; `ShapeStyle` for fills and backgrounds
- [ ] Modifier fusion (§9.4); pixel snapping of layout results
- [x] Text rendering: FreeType outlines → msdfgen MTSDF atlas (2048² RGBA8, 48px em, 6px
  range), HarfBuzz shaping, word wrapping, `line_limit` / height truncation with an ellipsis,
  multi-line alignment, baselines snapped to pixels
- [x] `Text` view; `Font` (system + semantic styles, `register_font()` + `Font::custom`);
  `font` / `foreground_style` as static environment values (`EnvironmentModifier` protocol).
  Unfilled shapes use the foreground.
- [x] One UI pipeline (`shaders/ui.*`) for rects and glyphs, in painter's order, in one draw call
- [ ] Text follow-ups: font fallback (emoji, CJK), bidi, UAX #14 line breaking, glyph generation
  on the worker pool, atlas eviction/pages, system font discovery (DirectWrite / CoreText /
  fontconfig), variable font weights, rich text (`Text + Text`), selection
- [ ] GPU paths (stencil-then-cover) for `Path` / custom shapes
- [x] Hit testing (SwiftUI semantics: a view is hit where its content draws; stroke-only shapes
  on their line; clips respected; `content_shape`, `allows_hit_testing`)
- [x] Events: `EventModifier` / `EventHandler` protocol, bubbling innermost-first, pointer capture
  between down and up, hover enter/leave; `on_tap_gesture` (single/double), `on_hover`
- [x] `Button` (default style: tint, hover, and pressed states) + `tint` environment value
- [x] Hover delays: `HoverDelay{.enter, .exit}`. Hover starts only after the pointer rests for
  `enter` (default 0.1 s), and ends `exit` after it leaves (default 0); passing over, or coming
  back in time, cancels the pending change.
  - Set for a subtree with `.hover_delay({...})` (an environment value), or per handler with
    `.on_hover(action, {...})`. Buttons follow the environment.
  - The idle loop wakes for the next pending hover change (an SDL wait with timeout) and
    otherwise stays at 0% CPU.
- [ ] Input follow-ups: synthetic input for UI tests (§16), drag/scroll gestures, cursors,
  keyboard and focus (M4)

**M2: Reactive**
- [x] Reactive core (`elg/reactive.h`): signals, computed values, and effects. Push-dirty /
  pull-values updates ("Reactively" style), so nothing recomputes more than once per change and
  no effect sees half-updated values. Effects are batched and run once per frame before layout;
  `batch`, `untrack`. Cycles are reported as `UsageError`.
- [x] `State<T>` (shared handle: lambdas capture it by value and still write; the constructor is
  explicit so `count = 1` assigns), `Binding<T>`, `Computed<T>` / `computed()`, `effect()`
- [x] Ownership: effects belong to the `Scope` of the window that built them and end with it.
  State is pinned to the UI thread while the app runs (`UsageError` otherwise).
- [x] Reactive arguments (`Prop<T>`: a value, a `State`, or a closure): `Text` content and
  color, `Shape::fill`, `background`, `opacity`. Each becomes one effect updating one element
  property: text changes relayout, colors and opacity only repaint.
- [x] `tests/` (Catch2, the `tests` vcpkg feature): reactive core tests
- [x] `If(condition, then, else)` and `ForEach(items, key, content)`: fragments whose children
  belong to the enclosing stack, an empty branch taking no space. Each item or branch is mounted
  in its own `Scope` and unmounted with it. ForEach keeps items whose key and value are
  unchanged, rebuilds changed ones, and removes the rest. Keys are integers, enums, or strings.
- [x] The event dispatcher forgets unmounted elements after each structural change.
  Known gap: it compares addresses, so an element allocated at a freed address in the same
  flush could be mistaken for the old one. Use generation IDs if this ever matters.
- [ ] More reactive arguments (frame sizes, padding; environment values in M3); `post_to_ui`
  for background threads

**M3: Environment & Animation**
- [x] Environment (`elg/environment.h`): `EnvironmentValues` is a persistent map of typed keys
  with stable string names; every value is a reactive argument. `Environment<Key>()` reads a key
  while views are built, and `.environment<Key>(value)` sets one.
  - Built-in keys: font, foreground, tint, color scheme, theme, disabled, button style.
- [x] Themes (`elg/theme.h`): `Theme` with light and dark `ColorTokens`. `ThemeColor` tokens
  work anywhere a color does and resolve where they're used. Windows follow the system light/dark
  setting reactively; `.color_scheme()` and `.theme()` override it.
- [x] Reactive arguments can also be closures taking `const EnvironmentValues&`, to mix state
  with theme colors: `[=](const EnvironmentValues& env) { ... env.color(ThemeColor::outline) }`.
- [x] `ButtonStyle` protocol (`elg/styles.h`): the button owns hover/press/enabled state; the
  style builds its look from a `ButtonConfiguration`. Built-in styles are `Prominent` (the
  default), `Bordered`, and `Plain`, all using only the public API. `.disabled()` is supported.
- [ ] Other style protocols as their controls arrive (`ToggleStyle`, `TextFieldStyle`, ...);
  theme fonts, spacing, and radii tokens; theme token files with hot reload (§8)
- [x] Animation (`elg/animation.h`): linear and ease curves (CSS cubic-Bézier), and springs
  with SwiftUI's `response` / `damping_fraction`.
  - Explicit: `with_animation(animation, changes)`. Effects scheduled by the writes run in that
    transaction.
  - Implicit: `.animation(animation, value)`, which applies when `value` changed in the same update.
  - Animated: fills, strokes, text color, opacity, and layout (frames ease to new positions and
    sizes).
- [x] Transitions: `opacity` (default), `scale`, `offset`, `move(edge)`, `slide`, `combined`,
  `asymmetric`. If/ForEach children animate in, and removed ones keep painting until their exit
  ends.
- [x] Snapping instead of easing:
  - A `Text` whose content changed snaps to its new frame (glyphs can't morph, so a gliding
    position looks wrong). Text whose content didn't change still eases, e.g. a row moving.
  - `.animates_layout(false)` makes a view and everything inside snap to new positions and sizes.
    Colors, opacity, and transitions still animate.
- [x] Frame pacing: while anything animates the loop renders every frame, paced by presents
  (vsync), then blocks again. Measured 0 ms CPU over 4 idle seconds.
- [ ] Animation follow-ups:
  - Retargeting carries velocity (springs currently restart from rest at the current value).
  - `matched_geometry_effect`, keyframes / `PhaseAnimator`, gradient interpolation.
  - Rotation transforms.
  - Each fragment child is wrapped for its transition, so a nested If/ForEach inside a ForEach
    item lays out on its own (vertically) instead of joining the outer stack.

**M4: Controls & Input**
- [x] Keyboard and focus:
  - Key events, focus, Tab traversal, and a focus-visible ring (§10).
  - Buttons respond to Space and Enter.
  - `.focusable()`, `.focused(binding)`, and `.on_key_press(...)`.
- [x] `Toggle(label, Binding<bool>)` with a `ToggleStyle` protocol:
  - `SwitchToggleStyle` is the default; `CheckboxToggleStyle` was the alternative, until
    `Checkbox` became its own control (below).
  - `.toggle_style(...)` applies a style.
  - A toggle reuses the button's behavior (click, Space, Enter), and the flip is animated.
  - The checkbox shows a filled mark rather than a check mark until vector paths land (and
    system fonts lack U+2713 without fallback).
- [x] `.offset(x, y)` is a reactive, animatable effect: it moves drawing and hit testing,
  without affecting layout. The switch knob uses it.
  - Known gap: Button/tap release-inside checks use the un-offset frame.
- [x] `TextField(placeholder, Binding<std::string>, {.on_submit})` with `TextFieldStyle`
  (`RoundedBorderTextFieldStyle` default, `PlainTextFieldStyle`). Details in §11.
  - `WindowServices` gives elements text input, the clipboard, cursor shape, and timed wakes.
  - Fix: `bind(...)` calls are qualified. Unqualified, ADL picked `std::bind` for
    `Prop<std::string>` rvalues and silently dropped the binding.
- [x] More core controls:
  - `Slider(Binding<float>, {.min, .max, .step, .on_editing_changed})`:
    - Drag anywhere on it, or use arrows, Page Up/Down and Home/End when focused (key changes
      animate).
    - The knob follows the binding, animated during animated updates.
  - `NumberInput(Binding<T>, {.min, .max, .step, .decimals, .format, .parse, .live,
    .shows_buttons, .scrubs})` for `int`, `float` and `double` replaced `Stepper`. Typing applies
    on Enter and on blur (or as typed with `.live`); invalid text reverts; values clamp, and `int`
    rounds. Up/Down step (Shift ×10), the style's stacked arrows are disabled at the bounds, and
    dragging sideways over the unfocused field scrubs. `.number_input_style(...)` takes
    `BoxNumberInputStyle` (default) or `PlainNumberInputStyle`.
  - `Progress(Prop<std::optional<float>>)` replaced `ProgressView`: a fraction, or, while it's
    empty (`Progress()`), activity of unknown length that renders continuously while shown. The
    fraction animates with the update that changed it. Presentation is the style's:
    `.progress_style(LinearProgressStyle{...})` (default) or `CircularProgressStyle{...}`, with
    track and fill colors and thickness; custom styles get `ProgressConfiguration::fraction`.
  - `Picker(Binding<T>, {{value, "Label"}, ...})` is segmented, built from Buttons; the selection
    change animates.
  - Follow-ups:
    - A menu presentation and `.picker_style(...)` (needs §13.1 popups).
    - NumberInput arrow auto-repeat while held.
    - A style protocol for Slider.
    - A sliding selection indicator for the segmented picker (needs matched geometry).
- [x] `ScrollView` / `ScrollView({.axes, .shows_indicators}, content)`:
  - Takes all the space it's offered. The content gets its ideal size along the scroll axes and
    the offered size across them (centered there), and is clipped.
  - Wheel notches ease; trackpad (fractional) deltas apply directly. A view at its end passes
    the scroll to an outer scroll view.
  - Keys unhandled by the focused view scroll it: arrows, Page Up/Down, Home/End.
  - Tab scrolls the newly focused view into sight (`Element::reveal`).
  - Scroll bars appear while scrolling or hovered, fade after a pause, and can be dragged.
  - Hover is re-evaluated after each frame, since content moves under the pointer.
  - Fix: event handlers now see the current frame time, so animations they start (scrolling,
    scroll bars) don't begin in the past.
  - Follow-ups: programmatic scrolling (`ScrollViewReader` / scroll position binding),
    overscroll bounce, momentum for wheel flicks, and scroll-aware synthetic input for tests.
- [x] ZStack parity with SwiftUI:
  - With mixed `layout_priority`, the highest-priority children size the stack and the others
    are offered that size, e.g. a shape behind `Text(...).layout_priority(1)` takes the text's
    size.
  - `.z_index(Prop<float>)` (reactive) sets stacking order among siblings in any stack. Higher
    values draw in front and are hit first; ties keep declaration order. Children animating out
    keep their z.
- [x] Secure entry, first `SecureField(placeholder, binding, options)`, now
  `TextField(placeholder, binding, {.secure = ...})` (a `Prop<bool>`, so it can be revealed):
  - Bullets, one per character; caret stops map back to the text's bytes.
  - No copy or cut. Word movement and double-click take the whole field. No IME composition.
  - One line: a static `.secure = true` with `.line_limit` above 1 throws `UsageError`.
- [x] `TextArea(placeholder, binding, options)`: the TextField editor in multiline mode. Enter
  starts a new line, Ctrl/Cmd+Enter submits; it takes the height it's offered (its text's in a
  ScrollView) and scrolls with the wheel, following the caret when it moves. Replaces
  `TextField` with a huge `max_lines`, which is now `line_limit` (a `Prop<int>`).
- [x] Fix: a TextField's text takes its font and colors from where the style places `field`, so
  a style's `.font()` and `.foreground_style()` apply.
- [x] Fix: `.focused(binding)` follows SwiftUI semantics.
  - The binding is true while focus is anywhere inside the view.
  - Setting it focuses the first focusable view inside.
  - The wrapper is a Tab stop only when nothing inside can take focus.
  - Before: the wrapper was its own extra Tab stop, so Tab into a wrapped field landed on an
    invisible element first and typing went nowhere.
  - Implemented with `FocusManager::watch` (focus entering or leaving a subtree) and
    `first_focusable`.
- [x] Example app `login-cpp` (in elegance-examples): a modern
  web-style sign-in screen.
  - A gradient backdrop with glows.
  - A card with a credentials form (custom TextField and Button styles, validation messages,
    Enter moving focus to the password via `.focused`).
  - A QR login panel: nayuki-qr-code-generator drawn with `Canvas`, with a logo `ZStack`ed on top.
  - An animated success screen.
- [x] `LazyVStack` / `LazyHStack({.alignment, .spacing}, ForEach(...))`:
  - Only items in or near the visible part of the enclosing ScrollView are built: the viewport
    plus half a viewport each way (`Element::visible_rect`). Items scrolled out of reach are
    destroyed with their scopes.
  - Sizes of built items are remembered by key. Items never built are estimated at the average
    measured size; the total re-settles over a frame as real sizes arrive.
  - When the ForEach's items change, built items whose key is gone or whose value changed are
    dropped (rebuilt if still in view).
  - Elements built or destroyed during layout are revalidated in the dispatcher right after
    layout.
- [x] `List(ForEach(...))` / `List({.position}, ...)`, since removed:
  - A lazy vertical stack in a ScrollView on a rounded surface.
  - Row padding, inset separators drawn by the stack, and a hover highlight.
  - Removed because none of that could be changed, and its highlight lagged behind the pointer
    (`.on_hover`'s enter delay, restarted for each row scrolling past). Lists are now
    `ScrollView(LazyVStack({.separators = StackSeparators{...}}, ...))` with rows styled like any
    view. Row selection is a later topic.
- [x] `ScrollPosition`: `scroll_to_top()`, `scroll_to_end()` and `scroll_to(id)` (a lazy item's
  key), passed as `ScrollViewOptions::position`.
  - Requests are applied in the scroll view's next layout.
  - `scroll_to(id)` re-aims for a few layouts while estimated sizes become real.
- [x] `ForEach` key functions go through `std::invoke`, so member pointers work
  (`ForEach(items, &Item::id, ...)`).
- [x] Fixes found by the 10,000-row demo:
  - The frame loop cleared `dirty` after building a frame, dropping redraws requested during
    layout. It's cleared before now.
  - Event results are applied as `if (call()) dirty = true;` so a handler's own redraw request
    can't be overwritten.
- [ ] Lazy follow-ups:
  - Insertion and removal transitions for lazy items.
  - Keep the scroll anchor stable when estimates above the viewport are corrected.
  - Keep a focused item built while it's scrolled away.
  - List selection (`selection: Binding`), sections and headers, swipe actions.
  - `LazyVGrid` / `LazyHGrid`.
- [ ] Grid, presentations (sheet/popover)
- [x] Popup presentation layer (§13.1):
  - Popups are `LiveWindow`s made with `SDL_CreatePopupWindow`. They're transparent, so their
    rounded panels and shadows go through the DirectComposition path on Windows.
  - Requests (`WindowServices::open_popup` / `close_menus` / `close_menus_then` /
    `close_tooltip`) are queued and carried out between events and layout.
  - Placement is at the point, below, or beside the anchor, flipped and shifted to stay within
    the display's usable area.
  - While a menu is open:
    - Keys go to the top menu: Up/Down/Home/End move focus, Enter/Space choose, Esc closes,
      Left leaves a submenu, Right enters one.
    - A press elsewhere closes the menus without passing the click on.
    - Moving or resizing the window, or focus leaving the app, closes them.
  - `.context_menu(builder)`: right-click (on release), the Menu key, or Shift+F10.
  - `Menu(items...)`, `MenuItem(title, action, {.shortcut, .destructive, .checked})`,
    `Submenu(title, builder)` (opens on hover after the delay, click, or Right), `Separator()`.
  - Choosing an item closes every menu, then runs its action.
  - `MenuButton(label, builder)`, and `Picker(..., PickerStyle::menu)`.
  - `.help("…")` tooltips: after 0.6 s; closed by leaving, pressing, scrolling, or typing.
  - Hovering a menu row focuses it, so the hover and keyboard highlights stay one.
  - Menu popups have zero hover delay: rows highlight on the first move, like native menus.
    Submenus open after resting 0.25 s (their own hover handler).
  - Composition swapchains presented with interval 0 here, to save a frame of hover latency.
    That left the frame-latency waitable unpaced (it signals once the present is queued), so
    animations ran at ~900 fps; they present with interval 1 again, paced by the waitable, and
    menu hover was checked to still feel immediate (§ presentation above).
  - `ButtonConfiguration::is_focused` lets styles show keyboard focus.
  - Icons come from Segoe MDL2 Assets on Windows, with fallbacks elsewhere (`view/icons.h`).
- [ ] Popup follow-ups:
  - Keep a popup window ready and reuse it, so opening doesn't build a D3D device and
    swapchain each time.
  - Type-to-select in menus; diagonal-move tolerance toward submenus.
  - `popover`, `sheet`, `ComboBox`.
  - `App::menu_bar` (native on macOS).
  - Opening on press instead of release on macOS.

**Rendering foundations**
- [x] Font fallback (§11):
  - Runs the requested font has no glyph for are re-shaped with the first system font that has
    each character, and glyphs carry their face.
  - Windows chain: Segoe UI Symbol, Segoe UI Emoji, Microsoft YaHei, Yu Gothic, Malgun Gothic,
    Nirmala UI, Ebrima, Gadugi, Segoe UI Historic, SimSun, Arial. macOS and Linux have their own
    candidate lists.
  - Fonts load lazily down the chain (CJK fonts only when CJK text appears), with a per-character
    cache.
  - Variation selectors and joiners nobody draws vanish instead of showing boxes.
  - CJK text breaks between characters.
- [x] Glyphs are generated on a worker pool:
  - The UI thread only reads the outline, computes metrics and reserves the atlas slot. Distance
    fields are generated on workers, and finished glyphs wake the loop to redraw.
  - New windows wait up to 1.5 s for their glyphs (popups 150 ms) before showing.
  - Workers: hardware threads − 1, up to 8. Debug builds use 3, because the debug heap's single
    lock made 8 workers 2.5× slower than 3.
  - Fonts are read in one bulk read. The stream-iterator read took ~1 s per CJK font in debug.
  - Measured debug startup of the sandbox: ~12 s → ~2.4 s. Release: 0.5 s.
- [x] Color emoji, COLRv0:
  - `FontFace` reads the default CPAL palette at load and looks up a glyph's COLR layers the
    first time it is drawn (`color_layers`).
  - `detail::draw_glyph` (`src/text/glyph_draw.h`) is the one glyph-drawing path for `Text` and
    text fields. It draws a color glyph as its layers, bottom to top, each in its palette color.
    "Foreground" layers take the text color, and every layer fades with the text's opacity.
  - Layer glyphs use single-channel distance fields, as icons do, since layers overlap themselves.
  - Fallback prefers a color font for emoji: characters that default to emoji presentation, or
    are followed by U+FE0F. U+FE0E keeps the text style, and symbols like ★ ♥ ✓ stay with
    Segoe UI Symbol.
- [x] Color emoji, COLRv1 (`src/text/color_glyph.cpp`):
  - FreeType reads the paint graph, and it is flattened once per glyph into filled regions: an
    outline (em units) plus a fill (`FontFace::color_paths`).
  - Supported paints: layers, glyph and color-glyph references, transform, translate, scale,
    rotate and skew (with their centers), solid colors and linear and radial gradients.
    Foreground colors take the text color.
  - The regions draw through the GPU path pipeline as `PathCommand`s, so emoji stay sharp at
    any size and their masks are cached like other paths.
  - Approximations:
    - Two-circle radial gradients become concentric ones around the end circle.
    - Sweep gradients draw as their average color.
    - Composite modes draw the source over the backdrop.
    - Gradient extend modes pad.
  - Windows 10's Segoe UI Emoji is COLRv0 only. Run `sandbox --emoji-font Noto-COLRv1.ttf` to
    see COLRv1 in the font fallback card.
- [ ] Color emoji follow-ups:
  - Sweep gradients and two-circle radials in the shader.
  - COLRv1 composite modes and clip boxes.
  - CBDT/sbix bitmap fonts (Apple Color Emoji, Noto Color Emoji's bitmap build).
  - Rasterizing big emoji runs to a cached layer instead of one mask per region.
- [x] GPU vector paths (§5), stencil-then-cover:
  - **Geometry** (`elg/path.h`, `src/geometry/path.cpp`):
    - `Path` building, including SVG-style arcs and rect/rounded-rect/ellipse/circle helpers.
    - `Path::svg(d)`: every command, relative and absolute, packed arc flags; errors give the
      position.
    - `bounds`, `contains(point, FillRule)`, `transformed`, `flatten(tolerance)` (Wang's formula).
    - `stroked(StrokeStyle)`: width, butt/round/square caps, miter/round/bevel joins with a miter
      limit, dash patterns with a phase.
    - The stroker emits a union of consistently wound pieces (segment quads, join pieces, caps)
      that the non-zero rule merges, so self-intersections need no special handling.
  - **Rendering:**
    - `PathCommand` carries shared flattened polygons placed by scale and offset, so scrolling
      and transitions don't re-flatten.
    - The planner turns each path into fan triangles and a mask pass.
    - `gpu::PathRenderer` counts winding into an MSAA stencil (incr/decr wrap by facing for
      non-zero, invert for even-odd), covers where non-zero or odd, and resolves to an R8 mask.
    - Stencil format: first supported of S8 / D24S8 / D32S8; samples: up to 8×.
    - `ui.frag`'s `path` kind fills color or gradient through the mask.
  - **Views:**
    - `PathShape(path, {.view_box, .fill_rule})`, scaled to fit and centered like SVG's "meet".
    - `CustomShape([](Rect r) { return Path...; })`, rebuilt per frame size.
    - `Shape::stroke(color, StrokeStyle)`: dashed strokes on any shape go through the path
      pipeline.
    - Hit testing on the flattened fill or stroke.
    - `GraphicsContext::fill_path` / `stroke_path` in a Canvas.
  - **Tests:** `tests/path_tests.cpp`.
- [x] Path mask caching:
  - Coverage masks are kept per window, keyed by a hash of the polygons, the scale, the
    sub-pixel offset (snapped to ¼ px) and the fill rule. Moving or scrolling a path reuses its
    mask.
  - Masks unused for 240 frames are dropped, and the least recently used ones go when the cache
    is over 32 MB. A mask still in flight is never freed.
  - Measured: a 75-frame scroll animation rendered 7 masks once and none after.
- [x] `clip_shape` with any path shape (`PathShape`, `CustomShape`):
  - The content renders into an offscreen layer.
  - A `PushLayerCommand.mask` path then multiplies the layer by its coverage mask, drawn with a
    "destination-in" pipeline variant of the UI shader. The layer then composites normally.
  - The mask quad covers the whole layer, and the sampler clamps to the mask's empty border, so
    everything outside the path clears.
  - Hit testing follows the path (winding test on the flattened fill).
  - Rounded-rect shapes (`RoundedRectangle`, `Capsule`, `Circle`) keep the analytic shader clip.
- [ ] Path follow-ups:
  - Skip MSAA for small masks under memory pressure.
  - Analytic-coverage AA for sharper thin strokes.
  - A compute rasterizer (Vello-style) if path-heavy scenes need it.
  - SVG icon sets via `register_icon_set({.svg_directory})`.
- [x] Persistent glyph cache on disk (`src/text/glyph_cache.*`):
  - Generated glyph pixels are kept in `%LOCALAPPDATA%\Elegance\GlyphCache`, or
    `~/Library/Caches/Elegance/GlyphCache` on macOS, or `$XDG_CACHE_HOME/elegance/glyphs` on
    Linux. `ELG_GLYPH_CACHE` overrides the location, and `ELG_GLYPH_CACHE=off` disables it.
  - There is one file per font, named by a hash of the font's bytes, so an updated font gets a
    fresh file. The header records the format version and the generator settings.
  - Records are appended by the workers with a checksum each. Loading stops at the first bad
    record and truncates there. Each file is capped at 64 MB.
  - A hit uploads the pixels straight away with no worker job.
  - Measured debug sandbox startup: 2.17 s cold, 0.68 s warm.

**M5: Polish**
- [x] Inspector (§16), first version (`src/view/inspector.*`, `src/view/inspect.cpp`):
  - F12 in any window opens an Inspector window for it. F12 again, or closing either window,
    closes it.
  - The tree lists every element (depth first, through `for_each_child`).
    - Each row shows the element's name and a summary: the text, a shape's radius, a stack's
      child count, effects like `clip_shape(Path)`, `opacity`, `shadow` and `offset`.
    - Rows also show the frame size and can collapse.
    - The tree follows the window, refreshing at most twice a second, and has a Refresh button.
  - Selecting a row highlights the element in its window.
  - Details for the selection: frame, last proposal, child count and the element's properties
    (font size, colors, fills, strokes, line count).
  - **Pick** mode: hovering in the window outlines the element under the pointer, a click
    selects it (expanding and scrolling the tree to it), and Esc cancels.
  - Frame stats: UI-thread build time, view count, display-list commands, glyphs and paths.
    Offscreen passes (layers) show as a warning.
  - Elements describe themselves with `virtual void Element::inspect(Inspection&)`. The default
    names an element after its type. The inspector keeps index paths, never element pointers,
    so it can't outlive what it shows.
  - It's a separate window rather than an overlay, so it never changes the inspected layout.
- [ ] Inspector follow-ups:
  - Environment values per element.
  - Modifier fusion.
  - Per-pass GPU timings.
  - **State inspection**, planned and deferred:
    - Tag each effect with the element being built when it was created, so a view's reactive
      sources (`sources_`) can be listed.
    - `State<T>` keeps a formatter when `T` is formattable, plus its `std::source_location` (or
      an optional name), so values show as `todos.cpp:42 · count = 3`.
    - For the selection: the states and computeds it reads, with values and the chain behind
      each computed. An "All states" tab with reader counts and change flashes, then editing
      simple values live.
    - Dev builds only.
- [x] Custom `Layout`, completed:
  - `CachedLayout<Cache>` (`elg/layout.h`), SwiftUI's makeCache / updateCache.
    - The cache lives in the laid-out view (`LayoutCache` in `LayoutElement`) and is made on
      first layout.
    - `update_cache` runs when children change (If/ForEach) or any descendant invalidates
      layout. It's kept across measure, place and frames.
    - Called without framework storage (by another layout), it builds a temporary cache.
  - Layout values: `view.layout_value<Key>(value)` and `LayoutChild::value<Key>()`, where a key
    is a type with `Value` and `default_value()`.
    - Carried by a layout-transparent `LayoutValueModifier<Key>`.
    - Found through the modifiers wrapping a child (effects, decorations, events, transitions),
      never inside nested layouts.
  - Sandbox: `FlowLayout` (wrapping tags) uses both, with a `RowBreak` value.
- [x] Hot reload and previews (§9.6), reset-on-reload:
  - `elg/hot_reload.h`:
    - `ELG_MODULE(root)` exports `elg_module_entry` (an ABI version, the root view and the
      module's previews).
    - `ELG_PREVIEW("Name") { ... }` registers into a per-module list (an inline function's
      static, so each DLL has its own).
    - `HotReload({.module, .build_command, .watch, .show})` is a window's content.
  - A reload is a `ForEach` over a one-item "generation" list, so the existing machinery
    unmounts the old tree (its scope and effects) and mounts the new module's views. No new
    element type and no App remount logic.
  - Modules load from unique copies (`%TEMP%/elegance-reload/<pid>/`), so the build can replace
    the file. The previous module stays loaded while its views fade out; older ones unload.
    Modules still loaded at exit are left loaded.
  - A watcher thread polls the watched folders every 250 ms.
    - A change runs the build command (output captured).
    - A new module file (from our build or an IDE's) reloads once it has settled.
    - Results reach the UI thread through `post_ui_task` (a queue the App drains each loop
      iteration, with a wake).
  - A status badge shows "Building…", the first compiler error (file, line, message) or
    "Reloaded (N ms)". A failed build keeps the last good views.
  - CMake: `elegance_views_module(target sources...)` and
    `elegance_hot_reload(host MODULE views [WATCH ...])`. The latter generates
    `<host>_hot_reload.h` with `elg_hot_reload_options()`. On Windows the rebuild runs through a
    generated `rebuild.cmd` that restores the configure-time compiler environment (vcvars'
    INCLUDE/LIB/PATH), so an app started from an IDE or Explorer can still build.
  - `elegance-preview` (apps/preview) shows a module's previews in a sidebar, or with `--root`
    its root view. `--module/--build/--watch` point it at any module. Ships with a
    `gallery_views` demo module.
  - Measured: about a 3 s edit-to-screen loop for the gallery module in a debug build.
- [ ] Hot reload follow-ups:
  - Restore `State` by view identity.
  - Detect host/module layout mismatches (an exported layout hash).
  - Copy PDBs so debuggers keep symbols.
  - Theme/token files that reload without a rebuild.
  - Catch exceptions from module bodies during mount (today they end the app, as in any view).
- [x] Accessibility (§12): AccessKit on Windows (UI Automation); element semantics for every
  built-in view; `.accessibility_*` modifiers; on-demand tree with actions. Verified through UI
  Automation and tested.
- [ ] Accessibility follow-ups:
  - macOS (NSAccessibility) and Linux (AT-SPI) adapters. AccessKit has them; they need the
    platform window handles.
  - Popups and menus as their own trees (menu, menu item roles).
  - Live regions (announcements).
  - Text selection and caret (text runs) for fields.
  - Lists and list items for `List`.
  - Headings in `Font::large_title` text.
  - Reduced motion and high contrast from the system settings.
  - Testing with Narrator and NVDA by a person.
- [ ] Docs & examples
- [x] Media (§13.3) part 1, raster images:
  - **API:**
    - `Image({.file})` and `Image({.data})`, with `scale` for @2x art. Exactly one source;
      `ImageSource{}` throws.
    - `.resizable()` applies to raster images only, before other modifiers, and throws
      otherwise.
    - `.aspect_ratio(ContentMode::fit/fill)` and `.aspect_ratio(ratio, mode)` work on any view
      (`AspectRatioModifier`). Without a ratio, the content's ideal size gives it.
  - **Decoding** (`src/media/image_decode`):
    - WIC on Windows: PNG, JPEG, GIF, BMP, TIFF, ICO, and WebP/HEIF/AVIF with the OS codecs.
      stb_image elsewhere for now (vcpkg, non-Windows).
    - Converted to straight-alpha RGBA. EXIF orientation applied. Larger than 8192 px scaled to
      fit.
  - **Loading** (`src/view/raster_image.cpp`):
    - The size is probed from the header when shown, so layout is right at once.
    - Pixels decode on a small worker pool and arrive through `post_ui_task` into a
      `State<pixels>` the element binds.
    - Assets are shared by source (file path + mtime, or a hash of the data), so an image shown
      twice decodes once.
    - A missing or undecodable image logs a warning and takes no space.
  - **GPU** (`gpu::TextureCache`):
    - Textures are `R8G8B8A8_SRGB` with a full mip chain (blits), uploaded inside the frame's
      own command buffer. Staging buffers live until that slot's fence.
    - Shared by all windows. Dropped after 600 idle frames, or LRU over a 512 MB budget (never
      within 16 frames of use).
  - **Drawing:** `ImageCommand` → the ubershader's `image` kind (sampled linear, premultiplied in
    the shader). Clip stack, `clip_shape`, opacity and layers all apply.
  - **Sandbox:** generated assets (landscape JPEG, PNG with alpha at @2x, a JPEG stored sideways
    with EXIF orientation 6) show fill + clip, fit, transparency and orientation.
  - **Tests:** source validation, `resizable()` and `aspect_ratio` usage errors.
- [ ] Image follow-ups:
  - Decode at the drawn size (no full-size decode for thumbnails).
  - Animated GIF/WebP.
  - SVG files through the path renderer.
  - `.interpolation(.none)` for pixel art.
  - ICC color profiles.
  - Premultiplied mip generation (straight-alpha mips can fringe at transparent edges).
- [x] Media part 2, audio (`elg/audio.h`, `src/media/audio.cpp`):
  - **miniaudio** 0.11 (vcpkg, header-only, compiled once in `media/miniaudio.cpp`). One
    `ma_engine` per process, started on first use. It follows the default output device.
    Without a device it logs once and plays silently.
  - **`Sound::load(file | data)`:** decoded once, to the engine's format, so playing never
    resamples. `play({.volume, .pitch, .pan})` starts a new voice, so plays overlap. Finished
    voices are swept on the next play, never on the audio thread.
  - **`AudioPlayer(file)`:** streamed (`MA_SOUND_FLAG_STREAM`). Has `play`, `pause`, `toggle`,
    `stop`, `seek`, `set_volume` and `set_looping`.
    - `is_playing()`, `time()`, `duration()`, `volume()` and `is_looping()` are signals.
    - `time_binding()` and `volume_binding()` for Sliders (scrubbing seeks).
    - Copies share one player, which stops with the last copy.
  - **Position updates:** a ticker thread posts them to the UI thread about 15 times a second,
    only while something plays, plus one final update when playback ends. Idle CPU stays 0.
  - **Formats:** WAV, FLAC and MP3. Ogg Vorbis, AAC and M4A are follow-ups (stb_vorbis, the
    platform decoders).
  - **Sandbox:** generated `click.wav` and `tune.wav`. Overlapping clicks with pitch and pan; a
    player with play/pause, scrubber, time, volume and loop.
  - **Tests:** a WAV built in memory decodes with the right duration; missing and garbage
    inputs throw `ResourceError`.
- [x] Media part 3, video (`elg/video.h`, `src/media/video_source.*`, `src/gpu/video_textures.*`):
  - **`VideoPlayer(path or URL)`:**
    - `play`, `pause`, `toggle`, `stop`, `seek`, `set_volume`, `set_muted`, `set_looping`.
    - Reactive `is_playing`, `is_ready`, `time`, `duration`, `natural_size`, `volume`,
      `is_muted`, `is_looping`, `error`.
    - `progress_binding()` (0...1, writing seeks) and `volume_binding()`.
    - Requests made before the decoder exists are kept and applied when it starts.
  - **`VideoView(player, ContentMode)`:** the picture fitted or filled (clipped) on black. Its
    ideal size is the video's (320×180 until known). **`VideoControls(player)`:** play/pause,
    a scrubber, time and mute, as ordinary views.
  - **Windows:** `IMFMediaEngine` in frame-server mode on a D3D11 device created on the
    renderer's adapter (by LUID, which the render thread publishes). It handles demuxing,
    hardware decoding, audio and A/V sync. Engine events (metadata, time updates, play, pause,
    end, errors) reach the UI states through `post_ui_task`. The event callback never takes the
    source's lock.
  - **Frames:**
    - `TransferVideoFrame` writes into a ring of 3 shared BGRA D3D11 textures (NT handles), so
      frames in flight keep theirs, and waits for the copy.
    - `gpu::VideoTextures` imports each texture once (`VK_KHR_external_memory_win32`, sRGB view)
      and samples it in place, with no copy.
    - Each command buffer acquires the textures from `VK_QUEUE_FAMILY_EXTERNAL` and releases
      them back at its end.
  - **Redraw without layout:** the render thread keeps the last frame of each window showing a
    video. While one plays it polls every 4 ms, and when the decoder has a new frame it redraws
    that frame: no UI-thread work per video frame.
  - **Startup:** a view emits its `VideoCommand` even before the size is known, since the render
    thread seeing it is what starts the decoder.
  - **Measured:** a 640×360 30 fps clip plays at about 9% of one core in a debug build with
    validation. Closing mid-playback exits cleanly with no validation errors.
  - **Other platforms:** a stub that reports "not available yet" (AVFoundation and GStreamer are
    next).
  - **Sandbox:** generated `clip.mp4` (ffmpeg testsrc2, H.264/AAC, 10 s), muted and looping, with
    controls, plus the same video filled into a circle.
  - **Tests:** requests before loading, copies sharing, views building.
- [ ] Video: the FFmpeg backend (§13.3, 3a). Optional `ELG_VIDEO_FFMPEG`, LGPL with dynamic
  linking, royalty-free decoders by default and H.264/HEVC/AAC only with
  `ELG_VIDEO_FFMPEG_PATENTED`.
  - Vulkan hwaccel zero-copy, with D3D11VA and software fallbacks.
  - Audio into miniaudio, audio-clock A/V sync, `VideoBackend::automatic` fallback.
  - Subtitles, track selection, thumbnails.
- [ ] Video follow-ups:
  - AVFoundation (macOS) and GStreamer (Linux).
  - GPU-side sync instead of CPU waits around `TransferVideoFrame`.
  - HDR/10-bit, subtitles and audio track selection.
  - Fullscreen.
  - Frame pacing from the display's vsync rather than 4 ms polling.
- [x] Media part 4, HTTP and `AsyncImage`:
  - **`elg/http.h`:** on the system's networking, with no bundled HTTP library.
    - WinHTTP on Windows: system certificates and proxy settings (automatic proxy where
      supported), HTTP/2, gzip/deflate, redirects. libcurl on Linux when present; NSURLSession
      on macOS is a follow-up.
    - Readable errors: timeout, name not resolved, TLS failure, cancelled...
  - **`http::fetch(Request, done)`:** a worker thread per request (up to 6). `done` runs on the
    UI thread. It returns a `Task`, and `cancel()` closes the WinHTTP handle so a blocking call
    stops at once.
  - **`http::Fetch`:** a request as reactive state (`is_loading`, `has_response`, `ok`,
    `response`, `error`, `reload`), cancelled with its last copy.
  - **`AsyncImage(url, {.scale, .content_mode})`** and `AsyncImage(url, content(phase))`:
    - Fetched when lowered (so List rows not built don't download) and cancelled when the view
      goes. Responses are checked to be images (`probe_image`).
    - Downloads are cached in memory by URL (64 MB LRU), so scrolling back doesn't fetch again.
    - The phase is the key of a one-item ForEach, so the spinner fades into the image. Failure
      shows `image-broken` with the error as a tooltip.
  - **Sandbox:** a Wikimedia photo (fill + rounded clip), a GitHub avatar in a circle, a failing
    `.invalid` URL, and an `http::Fetch` status line with Reload. Idle CPU stays 0.
  - **Tests:** header lookup, cancellation, AsyncImage construction and errors.
- [ ] Networking follow-ups:
  - NSURLSession (macOS).
  - A disk cache honoring `Cache-Control`/`ETag`.
  - Upload/download progress and streaming bodies.
  - WebSockets: `http::WebSocket` (elg/websocket.h) on WinHTTP; libcurl's WebSocket API elsewhere.
  - Optional JSON adapter (`elg/json.h` + `Response::json()`) behind a vcpkg feature, so apps
    that already use a JSON library don't get a second one.
- [x] Desktop integration (§13.4): file dialogs, drag and drop in, notifications and the tray
  icon, with a sandbox Desktop row and tests for the rules around them. The interactive parts
  (dialogs, toasts, tray menu, drops) still need a person to try them.
- [ ] Desktop follow-ups:
  - Dragging out (files and text to other apps).
  - WinRT toasts with actions.
  - Badge counts.
  - The app's own window icon and jump lists.
  - "Open with" / file associations (handling files passed at launch).
  - Single-instance apps.
  - The clipboard beyond text (images, files).
- [x] Renderer: exact clipping of rounded clips cut by rectangular ones.
  - Each instance carries the innermost rounded clip at its own size (`clip_rect` +
    `clip_radii`) and a `clip_scissor`, the intersection of every enclosing clip's rect. The
    shader multiplies the two coverages.
  - A circle half scrolled out of a ScrollView stays a circle with a straight cut, and a plain
    clip inside a rounded card keeps the card's corners. Before, the intersected rect took the
    rounded radii and gave a lens shape.
  - Still approximate: two rounded clips that overlap near the outer one's corners, where the
    outer corners act as its rect.
  - `QuadInstance` grows to 160 bytes.
- [x] Media part 5, capture (`elg/capture.h`):
  - **`AudioRecorder({.device_id, .sample_rate, .channels, .on_samples})`:**
    - miniaudio capture (`ma_device_type_capture`, f32) into memory.
    - `start`, `stop`, `toggle`, `clear`. Reactive `is_recording`, `duration`, `level` (the
      recent peak, posted about 15×/s while recording) and `error` (e.g. access denied in
      Settings > Privacy).
    - `samples()`, `wav()` and `save_wav()` (16-bit PCM), and `sound()` to play it back.
    - `on_samples` streams blocks on the audio thread.
    - `audio_input_devices()` lists inputs (id = the device id in hex).
  - **`Camera({.device_id, .width, .height, .mirrored})` and `CameraView(camera, mode)`:** a
    `VideoSource`, redrawn by the render thread without layout like video. Its frames go to the
    renderer as frames in memory (below).
    - Media Foundation's source reader on a capture thread, asking for NV12 (RGB32 if the
      camera can't). It picks the native mode closest to the requested size (larger preferred),
      then the best frame rate.
    - **DirectShow fallback** for cameras Media Foundation doesn't see, e.g. virtual cameras on
      Windows 10 (OBS, Snap Camera): camera → Sample Grabber (RGB32) → Null Renderer.
      `IAMStreamConfig` picks the closest format. The grabber's interfaces are declared here,
      since `qedit.h` left the SDK while `qedit.dll` still ships them.
    - DirectShow frames are taken in the camera's own format (RGB32, NV12, YUY2, I420). Forcing
      RGB32 made DirectShow insert converters that OBS's camera never fed. The graph runs
      without a reference clock, since the Null Renderer otherwise held frames until their
      presentation time and stalled the stream after the first one.
    - Frames stay in the camera's format, rows packed and top down (bottom-up RGB32 is
      flipped). The renderer converts them (below). A CPU converter (BT.709 / BT.601, limited
      range) remains for snapshots.
    - The preview is mirrored by default (uv read right to left). `snapshot()` encodes the newest
      frame, unmirrored, as PNG through WIC. `camera_devices()` lists Media Foundation's cameras
      plus DirectShow-only ones (ids prefixed `dshow:`).
  - **Verified on this machine:**
    - Recording from a (virtual) microphone: duration, level and 12 inputs listed.
    - OBS Virtual Camera (DirectShow, NV12 1920×1080 at 60 fps) shows live in `CameraView`.
    - CPU in release: 0.17 cores with the Vulkan conversion (0.35 in debug). Converting on the
      CPU took 1.25; the earlier D3D11 video processor took 0.11 (0.41 in debug) but was Windows
      only.
  - **Sandbox:** a Capture row (record / play / clear with a level meter; camera preview with
    start / stop / snapshot).
  - **Tests:** device listing, recorders and cameras before starting, WAV header, option
    clamping.
  - **Frame handoff, the same on every platform** (`media/video_source.h`): `VideoFrame` is
    tagged.
    - `d3d11_shared`: a shared BGRA texture, imported into Vulkan and sampled in place (Media
      Foundation video). macOS IOSurfaces and Linux DMA-BUFs would join as kinds imported the
      same way.
    - `cpu`: packed pixels in BGRA, NV12, YUY2 or I420, with matrix (BT.601 / BT.709) and range.
      Any camera or software decoder can produce these, so macOS and Linux backends only need
      to deliver bytes.
    - `VideoTextures` uploads a `cpu` frame into a host-visible storage buffer, then a fullscreen
      pass (`shaders/video_convert.frag`) reads its bytes and writes a BGRA image through a
      UNORM view. The frame is sampled through an sRGB view, so the YUV→RGB math stays in gamma
      space. It runs before the window's passes, once per new frame, even when several windows
      show the source.
    - Each source gets a ring of three images and buffers. It's recreated (after a device wait)
      when the size changes, and freed after 240 idle frames.
    - This replaced the D3D11 video processor, which was Windows only.
- [ ] Capture follow-ups:
  - macOS (AVCaptureSession) and Linux (PipeWire/V4L2), handing over `cpu` frames.
  - One less copy: capture straight into the upload buffer, which needs a buffer pool shared
    with the renderer.
  - The ring has three slots for every window. Many windows showing one source at once could
    overwrite a slot that's still in flight; per-slot fences would fix that.
  - Read the matrix and range from the camera's media type, rather than guessing from height.
  - Camera controls (focus, exposure), resolution and frame-rate lists.
  - A lock-free recorder buffer (the audio callback takes a mutex today).
  - Recording video (needs an encoder: Media Foundation's sink writer).
- [x] Built-in icon library (§13.2), **Phosphor** 2.1.2 (MIT):
  - Vendored verbatim in `third_party/phosphor`: regular, bold and fill TTFs plus the CSS name
    map, with the license and the upstream source. 1,530 icons.
  - Embedded with `elg_embed_file` (cmake/ElgEmbed.cmake). `ELG_ICONS=OFF` leaves it out.
  - `Image({.system_name = "phosphor.heart.fill"})` / `Image("heart")`, and
    `Label(title, {.system_name = ...})`.
  - Icons take the environment's font size, weight (bold uses the bold font) and foreground.
  - Unknown names throw `UsageError` with the three nearest names.
  - `register_icon_set(name, {.font, .weights, .variants, .names | .names_data, .replace})` and
    `set_default_icon_set`.
  - Name maps auto-detected: CSS (with the shared class prefix removed), flat JSON, and
    Material `.codepoints`.
  - Phosphor registers itself through that same call.
  - `register_font(family, std::span<const unsigned char>)`: fonts from memory.
  - `MenuItemOptions::image`.
  - Built-in check marks and chevrons use Phosphor, so the checkbox finally has a real ✓.
    Caption buttons keep Segoe MDL2 on Windows.
  - Glyph generation fixes for icon fonts:
    - Outlines in the PostScript direction (`FT_Outline_Get_Orientation`) are reversed before
      generating. Phosphor's had rendered inside-out.
    - Icon fonts (flagged by `register_icon_set`) get a single-channel true distance field with
      non-zero scanline sign correction, copied into all channels. Multi-channel fields turned
      their overlapping outlines into speckles and streaks.
    - Text keeps MTSDF.
    - Verified against FreeType's own rendering of the same glyphs.
  - Tests: `tests/image_tests.cpp` (names, variants, default set, suggestions, each map format,
    registration errors).
- [x] Phosphor thin, light and duotone (`ELG_ICONS_EXTRA_WEIGHTS`, ~1.6 MB more):
  - `Weight::thin` / `light` pick the matching font.
  - `"…duotone"` icons are two layers from duotone's own map: the CSS `:before` at 20% opacity
    and `:after`, drawn as a ZStack.
  - Icon sets can give variants their own name maps (`IconVariant`).
- [x] `View::font_weight(Weight)` changes the inherited font's weight (text and icons).
- [ ] Icon follow-ups:
  - Glyph-name maps from the font's `post` table; IcoMoon `selection.json` / Fontello configs.
  - The `elegance_icon_set` CMake helper for apps.
  - Generated `Icons::phosphor::...` constants.
  - Light/thin/duotone weights opt-in.
  - `.resizable()`.
  - Raster `Image({.file = ...})`; SVG sets with GPU paths.

---

## 19. Open Questions (summary)

1. ~~SDL3 vs native platform layer?~~ Decided: SDL3, behind an internal platform interface.
2. ~~Own layout engine vs Yoga/Taffy?~~ Decided: custom SwiftUI-style layout protocol.
3. ~~C++ DSL syntax style?~~ Decided: SwiftUI-style views + chained modifiers.
   Still open: camelCase aliases? Children syntax details?
4. ~~Hot reload strategy?~~ Decided: DLL reload + previews; token files for themes; no markup.
   Still open: how far to go with state preservation (§9.6)?
4a. ~~User dot-syntax extensions?~~ Decided: extension header included in `View` (§9.4).
    Pipe syntax: proposed no.
5. ~~Text: bitmap atlas vs MSDF vs hybrid?~~ Decided: MSDF (MTSDF) with a color-emoji bitmap
   atlas. Still open: a hinted small-size fallback, if measurements call for one.
6. ~~Vector paths: CPU or GPU?~~ Decided: GPU. Still open: technique (stencil-then-cover first,
   compute rasterizer later?).
7. ~~Separate render thread from day one?~~ Decided: yes, with a frame packet handoff (§15).
8. ~~How much C++26 to rely on?~~ Decided: C++23. Revisit when reflection ships everywhere (§3.1).
9. ~~Exceptions/RTTI policy?~~ Decided: developer errors propagate as exceptions, and internal
   errors produce a crash report (§3.2). Still open: crashpad for hard faults?
10. ~~Licensing?~~ Decided: MPL-2.0 (§3.4).
11. ~~Scripting layer?~~ Decided: no.
12. ~~Namespace?~~ Decided: `elg`.

---

## 20. Prior Art to Study

- **Declarative modifier-style UIs**: SwiftUI (primary reference: the layout protocol, the
  environment, styles, and the "Demystify SwiftUI" / "Compose custom layouts" WWDC talks),
  Jetpack Compose (Modifier chains, single-pass layout), Slint
- **Web-model native UIs** (for contrast): Sciter, Ultralight, RmlUi, NoesisGUI
- **Reactivity**: SolidJS, Leptos, Vue's reactivity, Preact Signals
- **Native retained UI**: Flutter (rendering pipeline, layout protocol), Slint, Qt Quick
- **GPU 2D rendering**: Vello, Skia Graphite, NanoVG, Blend2D, Rive renderer
- **Layout**: SwiftUI `Layout` protocol, Flutter's box constraints, Clay; Yoga/Taffy for contrast
- **Immediate-mode for contrast**: Dear ImGui, egui
