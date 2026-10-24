---
title: Hot reload and previews
description: Rebuilding a views DLL on save, reset-on-reload, ELG_PREVIEW, and the preview host.
sidebar:
  order: 2
---

Code is the only source of truth, so iteration speed comes from rebuilding a DLL of views and
reloading it. The framework rules that make this possible (copy, don't borrow; stable keys;
scoped ownership) must hold from the start.

:::note[Depends on]
The whole framework, with `elegance` built shared and `/MD`. The rules apply from day one; the
mechanism comes late.
:::

## Read first

- `design.md` §9.6 (Hot reload), M5 "Hot reload and previews"
- `include/elg/hot_reload.h`, `src/view/hot_reload.cpp`, `cmake/EleganceHotReload.cmake`, `apps/preview/`

## Tasks

### Rules to keep everywhere
- [ ] **Copy, never borrow**: the framework never keeps a `string_view` or pointer into user data (string literals live in the module's memory)
- [ ] **Stable keys**: environment keys, style IDs and modifier kinds use stable strings or hashes, never `typeid` or static addresses
- [ ] **Scoped ownership**: every closure, effect, subscription, timer and task belongs to a mount scope and can be torn down
- [ ] **One framework instance**: shared `elegance.dll`, same compiler and CRT
- [ ] **Light headers**: declarations only in public headers; precompiled headers

### Module side
- [ ] `ELG_MODULE(root)` exports `elg_module_entry` (an ABI version, the root view, the module's previews)
- [ ] `ELG_PREVIEW("Name") { ... }` registers into a per-module list (an inline function's static, so each DLL has its own)

### Host side
- [ ] `HotReload({.module, .build_command, .watch, .show})` as a window's content
- [ ] A reload is a `ForEach` over a one-item "generation" list, so the existing machinery unmounts the old tree and mounts the new one (no new element type)
- [ ] Load modules from unique copies (`%TEMP%/elegance-reload/<pid>/`) so the build can replace the file; keep the previous module loaded while its views fade out; unload older ones; leave modules loaded at exit
- [ ] A watcher thread polls watched folders every 250 ms; a change runs the build (output captured); a new module file reloads once it has settled
- [ ] Results reach the UI thread through `post_ui_task`
- [ ] Status badge: "Building…", the first compiler error (file, line, message), or "Reloaded (N ms)". A failed build keeps the last good views

### CMake and tools
- [ ] `elegance_views_module(target sources...)` and `elegance_hot_reload(host MODULE views [WATCH ...])`, generating `<host>_hot_reload.h` with `elg_hot_reload_options()`
- [ ] On Windows, rebuild through a generated `rebuild.cmd` that restores the configure-time compiler environment (vcvars' INCLUDE/LIB/PATH), so an app started from an IDE or Explorer can still build
- [ ] `elegance-preview`: a module's previews in a sidebar, or `--root` for its root view; `--module/--build/--watch` for any module; ships with a `gallery_views` demo module

## Done when

- [ ] Editing a color in the gallery module shows on screen in a few seconds (the design measured about 3 s in debug)
- [ ] A compile error shows in the badge and the last good UI stays up
- [ ] Twenty reloads in a row don't leak effects (check the reactive node count)

## Gotchas

- Changes to types shared with the host, to `main()`, or to dependencies need a full restart.
  Detecting that (an exported layout hash) is a follow-up.
- Statics in the module are re-initialized on each load.
- On macOS, `dlclose` may not actually unload, so load each module under a unique path and accept
  leaks in dev builds.
- Follow-ups: restoring `State` by view identity, copying PDBs so debuggers keep symbols, theme
  files that reload without a rebuild, and catching exceptions from module bodies during mount.
