---
title: Build, packages, and repo layout
description: "How the build is organized, why it's shaped that way, and how generated sources work."
sidebar:
  order: 1
depends: []
---

## What it's for

The build turns four kinds of input into one framework DLL plus the apps that use it: C++ sources,
GLSL shaders, binary assets (fonts, icon maps), and third-party packages. It has to be
reproducible from a clean clone with one command, and it has to keep a single copy of the
framework and C runtime in every process, because hot reload depends on that.

## How it works

### Layout

| Folder | Holds | Rule |
|---|---|---|
| `include/elg/` | The public API | Declarations only; no SDL, Vulkan or FreeType types |
| `src/` | The library, by subsystem (`core`, `platform`, `gpu`, `render`, `reactive`, `view`, `text`, `media`, `net`, `geometry`, `animation`) | Internal headers live next to their sources |
| `shaders/` | GLSL | Compiled at build time, embedded as SPIR-V |
| `apps/` | `sandbox`, `login`, `preview` | Link `elegence`, nothing else from `src/` |
| `examples/` | One `main.cpp` per example | Target `example_<folder>` |
| `tests/` | Catch2 tests | May include internal headers |
| `cmake/` | Build helpers | Shaders, embedding, extensions, hot reload |
| `third_party/` | Vendored data (Phosphor fonts) | Verbatim upstream with its license |

The public/internal split is the most important rule here. If a public header includes an
internal one, every app recompiles when internals change, which breaks the hot-reload latency
budget.

### Packages

vcpkg runs in **manifest mode**: `vcpkg.json` lists the dependencies, and its `builtin-baseline`
pins every port's version to one vcpkg commit. CMake loads vcpkg as its toolchain file through
`CMakePresets.json`. A per-machine `CMakeUserPresets.json` (gitignored) points `VCPKG_ROOT` at a
local checkout. Optional dependencies are vcpkg **features** (`tests`, `examples`, `profiling`,
`crash-reports`), and each CMake option that needs one is off unless that feature is enabled.

### The library target

`elegence` is a shared library by default (`ELG_BUILD_SHARED`). Exports go through a generated
`ELG_API` macro. Two details matter:

- **Per-member exports on `View` and `Shape`.** App code defines extension methods on these
  classes (see [Views, modifiers, and the element tree](../../framework/views-and-elements/)). MSVC
  refuses to let code outside a DLL define members of a whole-class `dllimport` (C2491), so those
  two classes export method by method.
- **`ELG_PRIVATE_MEMBERS_BEGIN/END`** wraps private standard-library members of exported classes
  to silence C4251. Those members are never touched across the DLL boundary because the methods
  that use them live in the DLL.

Every target uses the dynamic CRT (`/MD`). One process has exactly one heap, one `std::` runtime
and one copy of Elegence's globals. Hot-reload modules allocate objects that the framework frees,
and the reverse, which is only safe with one CRT.

All binaries go to one `bin/` folder. After building, `elegence` copies its own runtime DLLs
(SDL3 and others) there, because vcpkg only deploys an executable's *direct* dependencies.

### Generated sources

C++23 on MSVC has no `#embed`, so binary data becomes source code at build time:

- **Shaders**: `elg_add_shaders(target files...)` runs `glslc --target-env=vulkan1.3 -O -mfmt=num`
  per shader. The output is a comma-separated list of SPIR-V words, `#include`d into a
  `constexpr std::uint32_t` array. Each output depends only on its source, so editing one shader
  rebuilds one file.
- **Files**: `elg_embed_file(target symbol file)` runs a CMake script that writes
  `elg::detail::embedded::<symbol>` (bytes) and `<symbol>_size`. Code declares them `extern` and
  wraps them in a `std::span`.

## Rules

- A clean clone builds with `cmake --preset dev-debug && cmake --build --preset dev-debug`.
- No SDL, Vulkan, FreeType, HarfBuzz or miniaudio type appears in `include/elg/`.
- One CRT, one framework instance per process.
- Every source file carries the MPL-2.0 header (§3.4 of the design explains the license choice:
  file-level copyleft for Elegence, any license for apps).

## Edge cases

- **No MSVC environment** (a plain shell, CI without the developer prompt): the compiler can't
  find standard headers. Builds go through `vcvars64.bat`. Hot reload's generated `rebuild.cmd`
  restores that environment for the same reason.
- **A build tree without the manifest** (CLion's default profile) would pull packages from
  somewhere else. The top-level `CMakeLists.txt` refuses to configure there.
- **Static builds** (`ELG_BUILD_SHARED=OFF`) work for shipping, but hot reload needs the shared
  build, because each module would otherwise get its own copy of the framework's globals.
