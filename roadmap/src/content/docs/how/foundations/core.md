---
title: Errors, logging, and crash reports
description: "The two kinds of failure, how each travels, and the small core utilities everything uses."
sidebar:
  order: 2
depends: [foundations/build]
---

## What it's for

Every subsystem fails sometimes. This part decides, once, what happens when it does. A
developer's mistake or a missing file should reach the developer as an exception they can catch.
A bug inside Elegence should stop the process with a report good enough to fix it from, and never
limp on with corrupted state.

## How it works

### Two classes of failure

| | Developer-facing error | Internal error |
|---|---|---|
| Caused by | The app's code, its input, or the environment | A bug in Elegence |
| Examples | A font that doesn't exist, `State` written off the UI thread, no GPU | A broken invariant, an unexpected `std::out_of_range`, a segfault |
| Mechanism | `throw elg::Error` (or a subclass) | `ELG_ASSERT` → `internal_error()` → crash report → terminate |
| Caller can handle it | Yes | No |

`elg::Error` derives from `std::runtime_error`, with four subclasses: `ResourceError` (loading
failed), `GpuError` (no device, out of memory, device lost for good), `PlatformError` (the OS or
SDL failed), and `UsageError` (the API was misused). Debug builds capture a `std::stacktrace` at
the throw site. Where failure is routine, an API also has a `try_` form returning
`std::expected`.

**Classification rule.** `elg::Error` and `std::bad_alloc` escaping framework code are passed
on. Any *other* exception escaping framework internals means Elegence itself is wrong, so it
becomes a crash report.

### Exceptions crossing the framework

User code runs inside framework callbacks: `body()`, event handlers, effects, and later
`on_appear` and tasks. An exception from one of them unwinds to the **dispatch boundary**, the
framework function that called it. There:

1. The in-progress frame is discarded, so the element tree stays in its last consistent state.
2. The `exception_ptr` goes to `app.on_error(handler)`, which returns `ErrorAction::resume` (the
   app showed a toast, say) or `ErrorAction::quit`.
3. Without a handler, `App::run()` stops the loop, tears everything down in order, and rethrows
   to its caller. `try { app.run(); } catch (...) {}` works.

Exceptions never unwind through C code. Every callback Elegence hands to a C library (SDL event
watches, the Vulkan debug messenger, AccessKit, COM callbacks) is a trampoline that catches
everything, stores the `exception_ptr`, and returns normally. The exception is rethrown later on
the UI thread.

The render thread follows the same path: an `elg::Error` there stops the thread, is stored, and
wakes the UI loop, which rethrows it from `run()`.

### Internal errors and crash reports

`ELG_ASSERT(cond, fmt, args...)` is always on and cheap. `ELG_DEBUG_ASSERT` compiles away in
release. A failed assertion, an unreachable state, a validation error on a framework-owned Vulkan
object (debug builds), `std::terminate`, a pure virtual call, or a hard fault all end in
`internal_error(message, location, stacktrace)`, which:

1. breaks into the debugger if one is attached (debug builds),
2. writes `pref_path()/crashes/<timestamp>.txt` (and a minidump on Windows),
3. calls `app.on_crash(path)` if set, else shows a message box with the path,
4. terminates.

The report holds what's needed to debug without a repro: message, source location and stack
trace; version and build hash; OS, GPU, driver and Vulkan versions; window and DPI state; the
last ~200 log lines; recent input events and frame summaries; and loaded hot-reload modules.

`CrashHandlerScope` routes `std::terminate` to `internal_error` while the app runs and restores
the previous handler afterwards.

### Logging

`log_debug/info/warning/error(fmt, args...)` format with `std::format` and call
`log_message(level, text)`. It's thread-safe, writes to stderr, and keeps the last lines in a ring
buffer that crash reports read through `recent_log_lines()`.

### UI tasks

Background threads (decoders, HTTP workers, file watchers, the audio ticker) must never touch
reactive state. They call `post_ui_task(fn)`, which appends to a mutex-guarded queue and calls the
registered **waker**. The App registers a waker that wakes its blocked event loop
(`Platform::wake()`), and drains the queue with `run_ui_tasks()` between events. Tasks therefore
run on the UI thread, at a point where writing state is allowed. Apps get the same mechanism as
`post_to_ui_thread(fn)` (`elg/app.h`): tasks run in posting order, and tasks posted while no App
is running wait for the next one.

## Rules

- Never throw `elg::Error` for a framework bug, and never crash for a developer's mistake.
- No exception crosses a C frame.
- A failed user callback leaves the tree as it was before the frame started.
- Background threads talk to the UI only through `post_ui_task`.

## Edge cases

- **Memory corruption**: after a real fault, in-process code can't be trusted. Out-of-process
  capture (crashpad) is the open answer; the in-process reporter still covers assertions.
- **Errors during teardown**: once `run()` is rethrowing, further errors are logged, not thrown.
- **No App running** (unit tests): `post_ui_task` has no waker and tasks just queue; tests call
  `run_ui_tasks()` themselves.
