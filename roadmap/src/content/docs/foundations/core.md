---
title: Errors, logging, and crash reports
description: The error policy, assertions, the log ring buffer, crash reports, and UI-thread tasks.
sidebar:
  order: 2
---

The error policy is cheap to build now and painful to add later. Every subsystem after this one
throws, asserts, or logs through it.

:::note[Depends on]
[Build, packages, and repo layout](../build/).
:::

## Read first

- `design.md` §3.2 (Error handling)
- `include/elg/error.h`, `src/error.cpp`
- `src/core/log.*`, `src/core/crash.*`, `src/core/scope_exit.h`, `src/core/ui_tasks.*`

## Tasks

### Error types
- [ ] `elg::Error : std::runtime_error` with subclasses `ResourceError`, `GpuError`, `PlatformError`, `UsageError`
- [ ] Capture a `std::stacktrace` at the throw site in debug builds
- [ ] `try_` variants that return `std::expected` where failure is routine (e.g. `Font::try_load`)

### Assertions and internal errors
- [ ] `ELG_ASSERT(cond, fmt, ...)` (always on) and `ELG_DEBUG_ASSERT` (debug only)
- [ ] Classification rule: `elg::Error` and `std::bad_alloc` from framework code propagate; any other exception escaping framework internals is a bug and produces a crash report
- [ ] Break into the debugger first in debug builds when one is attached

### Logging
- [ ] Leveled log functions over `std::format`
- [ ] An in-memory ring buffer of the last ~200 lines, for crash reports

### Crash reports
- [ ] Write `pref_path()/crashes/<timestamp>.txt` with: message and location, stack trace, Elegance version/build hash, OS, GPU, driver, Vulkan version, window and DPI state, the log tail, recent input events, loaded hot-reload modules
- [ ] A minidump on Windows
- [ ] Install handlers for access violations, `std::terminate`, and pure virtual calls
- [ ] Default: `SDL_ShowSimpleMessageBox` with the path; `app.on_crash(fn)` to override

### Exceptions from user code
- [ ] Catch at every dispatch boundary (`body()`, event handlers, effects, `on_appear`, tasks) and discard the in-progress frame, so the tree stays consistent
- [ ] Forward the `exception_ptr` to `app.on_error(handler)`, which returns `ErrorAction::resume` or `ErrorAction::quit`
- [ ] With no handler: stop the loop, tear down cleanly, rethrow from `App::run()`
- [ ] Catch-all trampolines around every C callback (SDL event watches, the Vulkan debug messenger, AccessKit), so no exception unwinds through C frames

### Small utilities
- [ ] `ScopeExit` (run a lambda on scope exit)
- [ ] `post_ui_task(fn)`: a thread-safe queue the App drains each loop iteration, plus a wake of the event loop. Background threads (decoders, HTTP, file watchers) use it to reach the UI thread
- [ ] Public `post_to_ui_thread(fn)` (`elg/app.h`) over it for apps' own threads: tasks run in posting order, and tasks posted while no App runs wait for the next one

## Done when

- [ ] A test throws each error type and checks its message
- [ ] `try { app.run(); } catch (...) {}` catches an exception thrown from a button handler
- [ ] A deliberate `ELG_ASSERT(false)` writes a crash file containing the log tail and a stack trace
- [ ] A task posted from a worker thread runs on the UI thread and wakes an idle loop

## Gotchas

- Hard faults can't be trusted to in-process handlers after memory corruption. Crashpad
  (out-of-process) is still an open question behind the `crash-reports` feature.
- Leave RTTI on, but never use `typeid` or `dynamic_cast` for keys or in hot paths. Hot reload
  breaks type identity across DLLs.
