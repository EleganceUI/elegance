<h1 align="center">Elegance</h1>

<p align="center">
  <strong>Declarative, reactive desktop UI for C++23 and Rust, rendered on the GPU.</strong>
</p>

<p align="center">
  <a href="https://github.com/EleganceUI/elegance/actions/workflows/macos.yml"><img alt="macOS" src="https://github.com/EleganceUI/elegance/actions/workflows/macos.yml/badge.svg"></a>
  <a href="https://github.com/EleganceUI/elegance/actions/workflows/linux.yml"><img alt="Linux" src="https://github.com/EleganceUI/elegance/actions/workflows/linux.yml/badge.svg"></a>
  <a href="https://github.com/EleganceUI/elegance/actions/workflows/c-api.yml"><img alt="C API" src="https://github.com/EleganceUI/elegance/actions/workflows/c-api.yml/badge.svg"></a>
  <a href="LICENSE"><img alt="License: MPL-2.0" src="https://img.shields.io/badge/license-MPL--2.0-blue.svg"></a>
</p>

<p align="center">
  <a href="https://eleganceui.github.io/elegance-docs/">Documentation</a> ·
  <a href="https://github.com/EleganceUI/elegance-examples">Examples</a> ·
  <a href="https://github.com/EleganceUI/elegance-rs">Rust bindings</a> ·
  <a href="#contributing">Contributing</a>
</p>

---

Elegance is a cross-platform framework for building desktop applications. You describe the
interface as a tree of views, style it with chained modifiers, and bind it to reactive state:
when state changes, only the views that read it update. Rendering goes through WebGPU
([Dawn](https://dawn.googlesource.com/dawn)), on Metal, Direct3D 12, or Vulkan.

```cpp
#include <elg/elegance.h>

#include <format>

using namespace elg;

View Counter() {
    const State<int> count(0);

    return VStack({.spacing = 16},
                  Text([=] { return std::format("{}", count()); }).font(Font::system(56, Weight::bold)),
                  HStack(Button("−", [=] { count -= 1; }),
                         Button("+", [=] { count += 1; })));
}

int main() {
    App app;
    app.window({.title = "Counter", .width = 360, .height = 280}, Counter);
    return app.run();
}
```

> [!NOTE]
> Elegance is in early development (0.x). The API can change between releases.

## Features

- **Declarative views.** Stacks, grids, lists, navigation and split views, popovers, text
  fields, pickers, sliders, and more, composed as plain functions.
- **Fine-grained reactivity.** `State`, bindings, and computed values; updates touch only the
  views that depend on what changed.
- **Styling and theming.** Light and dark themes, button and view styles, custom modifiers.
- **Rendering.** Gradients, paths and shapes, a `Canvas`, backdrop blur, shadows, masks, blend
  modes, and animated transitions, all on the GPU.
- **Text.** HarfBuzz shaping, variable fonts, and the
  [Phosphor](https://phosphoricons.com) icon set built in.
- **Media and desktop integration.** Images (local and remote), video, audio, camera capture,
  file dialogs, notifications, tray icons, and drag and drop.
- **Accessibility.** Screen reader support through [AccessKit](https://accesskit.dev).
- **Hot reload.** Previews that rebuild and reload views as you edit.
- **Language bindings.** A stable C API (`c/`) that the
  [Rust bindings](https://github.com/EleganceUI/elegance-rs) are built on.

## Platform support

| Platform | Compiler | Notes |
| --- | --- | --- |
| Windows | MSVC (Visual Studio's C++ workload) | Direct3D 12 |
| macOS 13.3+ | Apple Clang from Xcode 26+ | Metal. Xcode 16's libc++ lacks `std::jthread` and `std::stop_token`. |
| Linux (X11, Wayland) | GCC 14+ or Clang 18+ | Vulkan. Video playback is not implemented yet. |

## Getting started

The quickest start is to copy a project from
[elegance-examples](https://github.com/EleganceUI/elegance-examples), such as `hello-cpp`.
Each example is a standalone CMake project that fetches Elegance and builds it alongside the app.

Rust developers should use [elegance-rs](https://github.com/EleganceUI/elegance-rs), which links
a prebuilt Elegance and needs no C++ toolchain.

The [documentation site](https://eleganceui.github.io/elegance-docs/) has guides for both
languages and the API reference.

## Building from source

### Prerequisites

- [CMake](https://cmake.org) 4.3 or newer, and [Ninja](https://ninja-build.org) (fetched
  automatically if missing)
- [vcpkg](https://vcpkg.io), used in manifest mode. The configure step uses `VCPKG_ROOT` if
  it is set, and otherwise clones its own copy.
- A compiler from the [platform support](#platform-support) table
- **macOS and Linux:** `pkg-config`
- **Linux:** the X11, Wayland, and libcurl development packages. The Ubuntu package list is in
  [`.github/workflows/linux.yml`](.github/workflows/linux.yml).

### Build

From a developer prompt (Windows) or a shell:

```sh
cmake --preset windows-debug        # see the table below
cmake --build build/windows-debug
build/windows-debug/bin/sandbox     # most features in one window
```

The first configure builds every dependency, Dawn included, which takes about half an hour.
Later builds reuse vcpkg's binary cache.

| Preset | Elegance library |
| --- | --- |
| `windows-debug`, `macos-debug`, `linux-debug` | Shared, which hot reload needs |
| `windows-release`, `macos-release`, `linux-release` | Static, linked into each app |
| `windows-release-fully-static` | Static, with the C runtime too: each app is one executable |

The macOS presets target Apple Silicon.

The [installation guide](https://eleganceui.github.io/elegance-docs/start/installation/) covers
every build option and how to use Elegance from your own CMake project.

### Tests

The unit tests use [Catch2](https://github.com/catchorg/Catch2) 3 and need a static build.
Enable vcpkg's `tests` feature, or point `Catch2_DIR` at an existing installation, then:

```sh
cmake --build build/local-offline-static
ctest --test-dir build/local-offline-static --output-on-failure
```

The checked-in [`.clangd`](.clangd) reads `build/local-offline-static/compile_commands.json`.
If you use another build directory, configure it with
`-DELG_BUILD_TESTS=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` and update `CompilationDatabase`.

## The C API and prebuilt libraries

The C API in [`c/`](c) is the interface language bindings call. Every build compiles it, so a
change to the C++ API that breaks it fails in the same pull request.

For each commit on `main`, [CI](.github/workflows/c-api.yml) publishes a `c-<commit>` prerelease
with two archives per target:

| Archive | Contents |
| --- | --- |
| `elegance-c-<target>.tar.gz` | One shared library that exports only the C API |
| `elegance-c-static-<target>.tar.gz` | One static archive holding the C API, Elegance, and every static dependency, along with the shared libraries that stay separate (AccessKit) and an `elegance-link.txt` manifest |

elegance-rs links the static archive into Rust executables. It pins a commit and moves the pin
through ordinary pull requests.

## Repository layout

```text
include/elg/    public C++ API
src/            implementation, by subsystem
c/              C API for language bindings, and its packaging
shaders/        WGSL shaders
cmake/          build helpers: hot reload, view extensions, link manifest
apps/sandbox/   feature showcase
apps/preview/   preview host for hot reload
tests/          unit tests (Catch2)
third_party/    vendored data (Phosphor icons)
```

## Related repositories

| Repository | Description |
| --- | --- |
| [elegance-rs](https://github.com/EleganceUI/elegance-rs) | Rust bindings with `elg!`, a SwiftUI-style DSL |
| [elegance-examples](https://github.com/EleganceUI/elegance-examples) | Example apps in C++ and Rust, one self-contained project per folder |
| [elegance-docs](https://github.com/EleganceUI/elegance-docs) | Source for the [documentation site](https://eleganceui.github.io/elegance-docs/) |

## Contributing

Bug reports, feature requests, and pull requests are welcome.

- **Bugs:** [open an issue](https://github.com/EleganceUI/elegance/issues) with a minimal
  reproduction, your platform and compiler version, and the relevant build output.
- **Pull requests:** keep each to one focused change, and run the tests before submitting. If
  you change the public API, update the C API in [`c/`](c) in the same pull request.

## License

Elegance is licensed under the [Mozilla Public License 2.0](LICENSE). Dependencies are
distributed under their own licenses; the Phosphor icons are MIT-licensed.
