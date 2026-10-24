# Elegance

A C++23 framework for desktop app interfaces. You describe the UI as a tree of views, style them
with chained modifiers, and connect them to reactive state; a WebGPU renderer (Dawn) draws the
result.

```cpp
#include <elg/elegance.h>

using namespace elg;

int main() {
    App app;
    app.window({.title = "Hello"}, [] {
        return VStack(Image("hand-waving").font(Font::system(48)),
                      Text("Hello, Elegance!").font(Font::title));
    });
    return app.run();
}
```

## Repositories

| Repository | What's in it |
| --- | --- |
| [elegance](https://github.com/EleganceUI/elegance) | The library (this one): `include/elg/`, `src/`, tests, the sandbox and the preview host, the docs |
| [elegance-rs](https://github.com/EleganceUI/elegance-rs) | Rust bindings, with `elg!`, a SwiftUI-style DSL |
| [elegance-examples](https://github.com/EleganceUI/elegance-examples) | Examples and apps in C++ and Rust, one self-contained project per folder |

## Building

Elegance builds with CMake 4.3+, Ninja, and [vcpkg](https://vcpkg.io) (manifest mode), with MSVC
on Windows or Apple Clang on macOS. With `VCPKG_ROOT` set, from a developer prompt:

```sh
cmake --preset windows-debug
cmake --build --preset windows-debug
build\windows-debug\bin\sandbox.exe
```

The first configure builds the dependencies, Dawn included (about half an hour). See
[Installation](docs/src/content/docs/start/installation.mdx) for the presets, the build options,
static linking, and using Elegance in your own project. To start an app, copy a folder from
elegance-examples, such as `hello-cpp`.

## Layout

```
include/elg/    the public API
src/            the library, by subsystem
shaders/        WGSL shaders
cmake/          build helpers: hot reload, view extensions, the link manifest
apps/sandbox/   most features in one window
apps/preview/   the preview host for hot reload
tests/          unit tests (Catch2)
docs/           the documentation site (Astro Starlight)
roadmap/        the roadmap site
third_party/    vendored data (Phosphor icons)
```

## License

[MPL-2.0](LICENSE).
