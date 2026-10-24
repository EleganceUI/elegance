---
title: Build, packages, and repo layout
description: CMake, vcpkg manifest mode, presets, shaders and embedded files.
sidebar:
  order: 1
---

Everything else sits on this. Get a library, a test app, and a test target building from one
command before you write any UI code.

:::note[Depends on]
Nothing. Install Visual Studio 2026 (MSVC 19.51+), the Vulkan SDK (for `glslc` and validation
layers), CMake and Ninja (CLion bundles both), and a vcpkg checkout.
:::

## Read first

- `design.md` §3 (Language & Build), §3.4 (License), §3.5 (Dependencies)
- `CMakeLists.txt`, `src/CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`
- `cmake/ElgShaders.cmake`, `cmake/ElgEmbed.cmake`, `cmake/ElgEmbedFile.cmake`
- `CLAUDE.md` (how to build from a shell without the MSVC environment)

## Tasks

### Repository and language
- [ ] Lay out `include/elg/` (public headers only), `src/` (library), `shaders/`, `apps/sandbox/`, `tests/`, `examples/`, `cmake/`, `third_party/`
- [ ] Require C++23 (`CMAKE_CXX_STANDARD 23`, no extensions). The design relies on deducing `this`, `std::expected`, `std::print`, `std::stacktrace`, `std::move_only_function`, `std::flat_map`
- [ ] Turn on `/W4` (or `-Wall -Wextra`) for the library from day one
- [ ] Add the MPL-2.0 `LICENSE` and the three-line MPL header to every source file

### Packages (vcpkg manifest mode)
- [ ] `vcpkg.json` with a pinned `builtin-baseline`
- [ ] Dependencies: `sdl3[vulkan]`, `volk`, `vulkan-headers`, `vulkan-memory-allocator`, `freetype`, `harfbuzz`, `msdfgen` (no default features), `miniaudio`, `stb` (non-Windows only)
- [ ] Optional features: `tests` (Catch2), `examples` (nayuki-qr-code-generator), `profiling` (Tracy), `crash-reports` (crashpad)
- [ ] `CMakePresets.json` that sets `CMAKE_TOOLCHAIN_FILE=$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake`
- [ ] A gitignored `CMakeUserPresets.json` with `dev-debug` / `dev-release` presets pointing `VCPKG_ROOT` at your local vcpkg
- [ ] Refuse to configure inside a build tree that doesn't use the manifest (the repo blocks `cmake-build-debug/`)

### Library target
- [ ] `ELG_BUILD_SHARED` option (ON by default): `elegance` builds as a DLL
- [ ] An `ELG_API` export macro (generated export header); export per member on `View` and `Shape`, not per class (see Gotchas)
- [ ] `ELG_PRIVATE_MEMBERS_BEGIN/END` to silence C4251 around private STL members of exported classes
- [ ] Use the dynamic CRT (`/MD`) everywhere: hot reload needs one CRT shared by host, framework and modules
- [ ] Output every binary into one shared `bin/` folder, and copy the framework's runtime DLLs (SDL3 and the rest) there after each build
- [ ] Options: `ELG_BUILD_SANDBOX`, `ELG_BUILD_EXAMPLES`, `ELG_BUILD_TESTS`, `ELG_ICONS`, `ELG_ICONS_EXTRA_WEIGHTS`, `ELG_ACCESSIBILITY`

### Build-time generation
- [ ] `ElgShaders.cmake`: compile each GLSL file in `shaders/` with `glslc` to SPIR-V, then embed it as a byte-array source (C++23 has no `#embed` on MSVC)
- [ ] `elg_embed_file(target NAME file)`: embed any file (fonts, icon maps) as a `std::span<const unsigned char>`
- [ ] Rebuild only when the source shader or file changes

### Targets
- [ ] `sandbox` app that links `elegance`
- [ ] `examples/<name>/main.cpp`, each built as `example_<name>`
- [ ] `tests` target with Catch2, registered with CTest

## Done when

- [ ] A clean clone configures and builds with `cmake --preset dev-debug` then `cmake --build --preset dev-debug`
- [ ] `sandbox.exe` runs from `bin/` without copying any DLL by hand
- [ ] Editing a `.vert`/`.frag` file rebuilds only its embedded source and the library
- [ ] The test target runs, even with zero tests

## Gotchas

- **`Cannot open include file: 'cstdint'`** from a plain shell means the MSVC environment isn't
  loaded. Run builds through a `.bat` that calls `vcvars64.bat` first.
- **MSVC C2491**: a `dllimport` class can't have members defined outside the DLL. View extensions
  (defined in app code) are exactly that, so `View` exports each method rather than the whole class.
- **vcpkg deploys only direct dependencies** of an executable. The DLL's own dependencies have to
  be copied next to it by a post-build step.
- C++26 isn't usable yet (no reflection, contracts or `#embed` in MSVC 19.51). Plan around it with
  macros and generated sources.
