---
title: Hot reload and previews
description: "Editing views while the app runs, and previewing views on their own."
---

Elegence has no markup language and no scripting layer. Fast iteration comes from **hot reload**:
your views live in a shared library (a *views module*), and the app rebuilds and reloads it when
its sources change. Save a file, and the window updates a moment later.

## Setting it up

Split the app into a views module and a host. In CMake:

```cmake
# The views: a shared library.
elegence_views_module(todo_views views/root.cpp views/todo_list.cpp)

# The host: the executable that shows them.
add_executable(todo main.cpp)
target_link_libraries(todo PRIVATE Elegence::Elegence)
elegence_hot_reload(todo MODULE todo_views WATCH views)
```

`elegence_hot_reload` generates a header for the host, `todo_hot_reload.h`, with the module's path,
the command that rebuilds it, and the folders to watch (the module's source folder by default).

In the views module, export the root view once:

```cpp title="views/root.cpp"
#include <elg/elegence.h>

using namespace elg;

View Root() {
    return TodoList();
}

ELG_MODULE(Root)
```

In the host, show it with `HotReload`:

```cpp title="main.cpp"
#include "todo_hot_reload.h"

#include <elg/elegence.h>

int main() {
    elg::App app;
    app.window({.title = "Todos"}, [] { return elg::HotReload(elg_hot_reload_options()); });
    return app.run();
}
```

Run the host and edit a file in `views/`. A small badge shows the build's progress, and any
compiler errors; when the build succeeds, the module is reloaded and the window shows the new
views.

### What survives a reload

State created in the views resets when the module reloads, since the code that made it is gone.
Keep data that should survive in the host, and pass it in (or put it in the environment).

The host and the module must be built with the same compiler, runtime and Elegence, which is
automatic when they're in the same CMake project. Elegence must be built as a shared library
(`ELG_BUILD_SHARED`, the default).

## Previews

Register previews anywhere in a views module: views shown on their own, with sample data, like
SwiftUI previews.

```cpp
ELG_PREVIEW("Empty list") { return TodoList({}); }
ELG_PREVIEW("Dark mode") { return TodoList(sample_todos()).color_scheme(ColorScheme::dark); }
```

Show them with `HotReloadShow::previews`: a list of the module's previews beside the selected one,
reloaded as you edit:

```cpp
elg::HotReloadOptions options = elg_hot_reload_options();
options.show = elg::HotReloadShow::previews;
app.window({.title = "Previews"}, [=] { return elg::HotReload(options); });
```

A module made only for previews uses `ELG_MODULE(nullptr)`.

The repository's `elegence-preview` app is such a host. Run it with `--module <file>`,
`--build "<command>"` and `--watch <folder>` to preview any module.
