---
title: Hot reload and previews
description: "Views in a reloadable DLL, the framework rules that make unloading safe, reload as a ForEach change, the watcher, and the preview host."
sidebar:
  order: 2
depends: [framework/dynamic-content, foundations/build]
---

## What it's for

Iteration speed without markup or scripting. The app's views live in a **views module** (a
shared library). Saving a source file rebuilds the module and swaps the running app's views for
the new ones in seconds. Previews show individual views on their own.

## How it works

### The setup

```
app.exe / preview host ──links──► elegance.dll        (never reloaded)
        └─ LoadLibrary ──► views_<N>.dll             (rebuilt and reloaded on save)
```

The module is built against the **same** `elegance.dll`, compiler and CRT as the host. It exports
`elg_module_entry`, made by `ELG_MODULE(root)`, which returns an ABI version, the root view
function and the module's previews. `ELG_PREVIEW("Name") { ... }` registers a preview into a list
that's an **inline function's static**, so each DLL has its own list.

### Why unloading is safe: the framework rules

Unloading a DLL frees its code and static data. Anything the framework still references in it
would crash. Five rules, kept everywhere from the start, prevent that:

1. **Copy, never borrow.** The framework copies every string and value it's given and never keeps
   a `string_view` or pointer into user data. String literals live in the module.
2. **Stable keys.** Environment keys, style ids and modifier kinds are identified by stable names
   or hashes, never `typeid` or static addresses, which differ per module.
3. **Scoped ownership.** Every effect, closure, subscription, timer and task belongs to a mount
   scope, so the module's code can be torn down completely.
4. **One framework instance.** The shared `elegance.dll` with `/MD`, so host, framework and
   module share one heap and one set of globals.
5. **Light headers.** Public headers declare; reload latency is compile + link time.

### Reload as a ForEach change

`HotReload({.module, .build_command, .watch, .show})` is a view. Its content is **the single item
of a ForEach keyed by the module's generation**. Loading generation N+1 is just a ForEach whose
key changed: the existing machinery unmounts the old tree (destroying its scope and effects) and
mounts the new module's views, with a transition. No special element or App-level remount logic
is needed.

Loading a module:

1. Copy the module file to a **unique path** under `%TEMP%/elegance-reload/<pid>/`. A loaded DLL
   is locked on Windows, and `dlopen` caches by path elsewhere, so loading from a copy lets the
   build overwrite the original.
2. `LoadLibrary`, find `elg_module_entry`, check the ABI version.
3. Copy the preview names (they live in the module).
4. Bump the generation.
5. **Unload modules older than the previous one.** The previous module stays loaded while its
   views fade out. Modules still loaded at exit stay loaded, since their elements may be torn
   down after this.

### The watcher

A background thread polls the watched paths **every 250 ms**, recording last-write times of
every file:

- **Sources changed**: run `build_command` through the shell, capturing stdout and stderr. On
  failure, extract the first line that reports an error (file, line, message).
- **The module file changed** (from this build, or an IDE's): wait until it has settled, then
  reload.

Results go to the UI thread with `post_ui_task`. A **status badge** shows "Building…", the first
compiler error, or "Reloaded (N ms)". A failed build keeps the last good views on screen.

### CMake

- `elegance_views_module(target sources...)` builds a module.
- `elegance_hot_reload(host MODULE views [WATCH ...])` generates `<host>_hot_reload.h` with
  `elg_hot_reload_options()` (paths and the build command).
- On Windows the build runs through a generated `rebuild.cmd` that restores the configure-time
  compiler environment (vcvars' INCLUDE, LIB, PATH), so an app started from Explorer or an IDE can
  still build.

### Previews

`elegance-preview` (apps/preview) loads any module (`--module/--build/--watch`) and shows its
previews in a sidebar, rebuilding the selected one when the selection or the module changes.
`--root` shows the module's root view instead. It ships with a `gallery_views` demo module.

### State across reloads

State **resets** on reload, as SwiftUI Previews do. The recommended pattern: keep app data in the
host (stores in the environment) and views in the module, so a reload changes how things look
and keeps the data. Restoring `State` by view identity is a possible later step.

## Rules

- Follow the five framework rules everywhere, from the first line of the framework.
- Never unload a module whose views may still be on screen.
- A failed build never replaces working views.

## Edge cases

- **Changes to types shared with the host**, to `main()`, or to dependencies need a restart.
  An exported layout hash could detect this.
- **Statics** in the module are re-initialized on every load.
- **Exceptions in a module's body** during mount currently end the app, as in any view.
- **macOS `dlclose`** may not unload; unique paths and accepted leaks cover it in dev builds.
