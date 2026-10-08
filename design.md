# Elegence — Design Document

> Status: **Draft / living document**
> Last updated: 2026-10-08

Elegence is a C++ UI framework for building modern app interfaces in a SwiftUI-style declarative
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
- **OPEN**: Native-looking widgets (OS-themed)? Proposed: no. Elegence draws everything itself.

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
  - *`#embed`*: SPIR-V shaders and prebuilt font atlases are embedded through a CMake step that
    generates byte-array sources.
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

**2. Internal errors produce a crash report, then terminate.** These are bugs in Elegence
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
  - The Elegence version and build hash, the OS, and the GPU, driver, and Vulkan version.
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
- **Shaders**: GLSL is compiled offline with `glslc` (from the Vulkan SDK, or the vcpkg
  `shaderc` port), and the SPIR-V is embedded via generated sources.
- **Library type**: `SHARED` by default, with static supported. The ABI surface uses `ELG_API`
  exports and pImpl at boundaries.
- **Namespace**: `elg::` (decided). Macros use an `ELG_` prefix.
- **Local setup**: the project uses the dedicated vcpkg at `G:\elegence-vcpkg`, which is also
  the source of the `builtin-baseline` commit. `CMakeUserPresets.json` (gitignored, per-machine)
  defines `dev-debug` / `dev-release`, which set `VCPKG_ROOT` to that path. Other machines
  either set `VCPKG_ROOT` or add their own user presets.
- **Runtime DLLs**: vcpkg only deploys an executable's direct dependencies. So `elegence` copies
  its own runtime DLLs (SDL3, …) into the shared `bin/` folder after each build.

### 3.4 License: MPL-2.0 (decided)
- Elegence is licensed under the **Mozilla Public License 2.0** (`LICENSE` at the repo root).
- **Intent**: no closed, paid versions of the framework. Anyone who distributes a modified
  Elegence must publish their changes to Elegence's files under MPL-2.0.
- **Apps built with Elegence** can use any license, including closed-source and paid. MPL is
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
| Vulkan loading    | volk                                    | Avoids linking the loader statically |
| Memory allocation | VulkanMemoryAllocator (VMA)             | Industry standard                    |
| Shader compile    | Offline: glslc/slangc → SPIR-V embedded | Avoids a runtime compiler dependency |
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
- **Threading constraint**: SDL requires window creation and event pumping on the main thread,
  so the **UI thread is the main thread** (§15).
- **Live resize**: on Windows and macOS the OS blocks the main thread in a modal loop while the
  user drags the window edge. Handle this with SDL's event watch callback, which triggers layout
  during the drag, and the render thread keeps presenting. The window must never freeze or
  stretch while resizing.
- **Custom title bars**: use SDL3's `SDL_SetWindowHitTest` for borderless windows with
  draggable regions.
- **Embedding mode**: the host supplies `VkDevice`, `VkQueue`, and a target image. Elegence
  records into a command buffer the host provides. No platform layer is needed in this mode.

---

## 5. Renderer (Vulkan)

### Baseline
- Target **Vulkan 1.3** (dynamic rendering, synchronization2), with a fallback path for 1.2 plus
  extensions where needed. macOS goes through MoltenVK and the portability extension.
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

| SwiftUI              | Elegence                          | Underlying                        |
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
- `Spacer()` (flexible gap) and `Divider()`.
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
The same pattern applies to `ToggleStyle`, `TextFieldStyle`, `ProgressViewStyle`, `ListStyle`,
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
#include <elg/elegence.h>
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
Elegence does the same:
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
C++ has no extension methods, and C++26 adds no member injection or UFCS. Elegence gets real
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

**2. Define them as normal code**, either in a header after `<elg/elegence.h>` (mark them
`inline`) or in any `.cpp` of the module:
```cpp
// app/view_extensions.cpp
#include <elg/elegence.h>
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
elegence_view_extensions(my_app_views app/view_extensions.h)
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
| Effects          | `opacity`, `shadow`, `blur`, `brightness`, `saturation`, `blend_mode`,  | layer paint props;        |
|                  | `rotation_effect`, `scale_effect`, `background_material` (blur behind)  | offscreen only if needed  |
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
- `clip_shape(Path)`: stencil-based, which is cheap.
- `mask(View)`: an arbitrary alpha mask. This needs an offscreen layer, so it's the most expensive
  option and the inspector should flag it.

### 9.5 Shapes, color, fonts, animation
- **Shapes** are views: `Rectangle`, `RoundedRectangle(r, CornerStyle::continuous)` (squircle),
  `Capsule`, `Circle`, `Ellipse`, `Path`, and `UnevenRoundedRectangle`. Modify them with
  `.fill(style)`, `.stroke(style, width)`, and `.inset(by)`.
- **ShapeStyles**: `Color` (sRGB/P3, `hex`, `opacity`, semantic tokens), `LinearGradient`,
  `RadialGradient`, `AngularGradient`, `ImagePaint`, and `Material` (frosted blur).
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
app.exe / preview host  ──links──►  elegence.dll        (never reloaded)
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
4. **One framework instance.** Hot reload builds use the shared `elegence.dll` (dev builds force
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
- **Proposed**: Integrate **AccessKit** via its C API rather than writing three backends.
- Built-in components ship with correct roles. Keyboard navigation works everywhere, and
  `prefers-reduced-motion` and high-contrast settings are respected.

---

## 13. Component Library (v1 candidates)

| Category   | Components                                                                 |
|------------|----------------------------------------------------------------------------|
| Layout     | VStack, HStack, ZStack, Spacer, Divider, Grid, LazyVStack/HStack,          |
|            | LazyVGrid/HGrid, ScrollView, ViewThatFits, GeometryReader, HSplitView      |
| Shapes     | Rectangle, RoundedRectangle, Capsule, Circle, Ellipse, Path                |
| Basic      | Text, Label (icon + text), Image, AsyncImage, Link, Badge                  |
| Input      | Button, TextField, SecureField, TextEditor, Toggle, Slider, Stepper,       |
|            | Picker (menu/segmented/radio), DatePicker, ColorPicker                     |
| Data       | List (virtualized, sections, selection), Table, OutlineGroup (tree),       |
|            | DisclosureGroup                                                            |
| Overlay    | sheet, popover, alert, confirmation_dialog, Menu, context_menu, help       |
|            | (tooltip), Toast                                                           |
| Feedback   | ProgressView (linear/circular), Gauge, redacted (skeleton)                 |
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
            Divider(),
            Submenu("Move to", ForEach(folders, key, [=](const Folder& f) { return MenuItem(f.name, move_to(f)); })),
            MenuItem("Delete", remove).role(ButtonRole::destructive).disabled([=] { return locked(); }));
    })
```

- The builder runs each time the menu opens, in a fresh `Scope` disposed on close. Items reflect
  current state, and nothing stays subscribed while the menu is closed.
- Item actions run after the menu has closed, so an action can open a dialog or another menu.
- Items: `MenuItem` (title, optional icon, shortcut hint, `disabled`, `checked`, `role`),
  `Divider`, `Submenu`, and `Section` (a titled group).

#### Rendering: drawn by Elegence, in a popup window (decided)

| Option | Pros | Cons |
|---|---|---|
| Native OS menus (`TrackPopupMenu`, `NSMenu`) | Exact platform feel | Unstyleable, no custom views, nothing native on Linux/Wayland, contradicts "Elegence draws everything" (§1) |
| Overlay drawn inside the window | Simplest; uses the top layer | Clipped to the window: breaks near edges and in small windows |
| **Elegence-drawn, in an SDL popup window** | Extends past the window, fully styleable, Wayland via `xdg_popup` | A small window with its own swapchain per popup |

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
- **Embedding mode** (§4) runs without the render thread: the host calls `elg::render(cmd)` on
  its own render thread with the latest packet.
- **Rule**: the UI thread never touches Vulkan objects, and the render thread never touches the
  element tree or signals.

---

## 16. Developer Experience

- **Hot reload / previews**: see §9.6.
- **Inspector**: an in-app overlay (toggle with F12) with the view tree, each view's modifier
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
- [ ] `examples/`
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
- [x] User extensions via `elegence_view_extensions()` (the sandbox's `.card()`)
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
  - `SwitchToggleStyle` is the default; `CheckboxToggleStyle` is the alternative.
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
  - `Stepper(label, Binding<int>, {.min, .max, .step})`: composed from the public API; −/+ are
    disabled at the bounds.
  - `ProgressView(Prop<float>)` is determinate and animates with its value. `ProgressView()` is
    indeterminate: it renders continuously while shown.
  - `Picker(Binding<T>, {{value, "Label"}, ...})` is segmented, built from Buttons; the selection
    change animates.
  - Follow-ups:
    - A menu presentation and `.picker_style(...)` (needs §13.1 popups).
    - Stepper auto-repeat while held.
    - Style protocols for Slider and ProgressView.
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
- [ ] Lazy stacks/List, Grid, presentations (sheet/popover)
- [ ] Popup presentation layer (§13.1): popup windows, `context_menu`, `Menu` / `MenuItem` /
  `Submenu`, menu keyboard navigation, `.help()` tooltips; `App::menu_bar` (native on macOS)

**M5: Polish**
- [ ] Inspector, accessibility, previews / hot reload, custom `Layout`, docs & examples

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
