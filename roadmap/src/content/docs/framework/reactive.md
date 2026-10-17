---
title: Reactive core
description: Signals, computed values, effects, scopes, State, Binding, and Prop.
sidebar:
  order: 1
---

A SolidJS-style fine-grained graph. A view body runs **once**; reactive arguments become effects
that update exactly one property on one element. There is no diffing.

:::note[Depends on]
[Errors, logging, and crash reports](../../foundations/core/). This page is pure C++ with no GPU,
so you can build and test it in parallel with the renderer.
:::

## Read first

- `design.md` §6 (State & Reactivity), §9.3 (Reactive arguments), M2
- `include/elg/reactive.h`, `src/reactive/runtime.*`, `include/elg/prop.h`, `tests/reactive_tests.cpp`
- The "Reactively" algorithm by Milo (push-dirty / pull-values), SolidJS, Preact Signals

## Tasks

### The graph
- [ ] `ReactiveNode` of kind signal, computed, or effect, with `sources_` and `observers_`
- [ ] States `clean` / `check` / `dirty`. A write marks direct observers `dirty`, and everything further down `check`
- [ ] `track()`: subscribe the current observer (and bring a computed up to date first)
- [ ] `update()`: for `check`, update sources in order until one turns this node `dirty`; for `dirty`, re-run
- [ ] Re-track dependencies on every run (unlink all sources first)
- [ ] A computed whose value didn't change (`==`) doesn't dirty its observers
- [ ] `dispose()`: unlink both ways and remove from the pending queue

### Effects and scheduling
- [ ] Pending effect queue; `flush_effects()` runs them. The framework calls it once per frame, before layout
- [ ] Priority effects go to the front of the queue (implicit animation triggers need this)
- [ ] `batch(fn)` (effects run once when the outermost batch ends) and `untrack(fn)`
- [ ] Cycle guard: after 100,000 runs in one flush, throw `UsageError` ("an effect probably writes state it also reads")
- [ ] Exceptions from an effect leave the graph consistent (reset the observer, mark the node clean, rethrow)

### Ownership
- [ ] `Scope` owns effects and disposes them on destruction; `Scope::Use` makes one current
- [ ] `start_effect` throws `UsageError` when there's no current scope ("effect() must be called while building views")
- [ ] Pin state to the UI thread while an App runs: `check_ui_thread()` throws `UsageError` elsewhere. Before `run()`, any thread may build views (tests)

### Public types
- [ ] `State<T>`: a cheap shared handle. `operator()` tracks, `peek()` doesn't, plus `set`, `update(fn)`, `=`, `+=`, `-=`. Skip notifying when the new value is equal
- [ ] Make the `State` constructor `explicit`, so `count = 1` assigns instead of building a new State
- [ ] `Binding<T>`: a get/set pair; built from a `State`; `Binding::constant(v)` for previews
- [ ] `Computed<T>` and `computed(fn)`; `effect(fn)`
- [ ] `Prop<T>`: a variant of a static value, a closure returning `T` (also States, Bindings, Computeds), or a closure taking `const EnvironmentValues&`; plus a `ThemeColor` overload for color-like `T`
- [ ] Transaction hooks for animation: `begin_transaction` / `end_transaction` / `current_animation`, `current_flush`, `take_flush_animation`. Effects remember the transaction that scheduled them

## Done when

- [ ] Diamond test: `a → b, a → c, (b, c) → d`. Writing `a` runs `d` once and it never sees a half-updated value
- [ ] Unmounting a scope stops its effects, even ones already queued
- [ ] Writing a `State` from a `std::thread` while the app runs throws `UsageError`
- [ ] An effect that writes what it reads throws instead of hanging

## Gotchas

- Inside `bind(...)`, **qualify the call** (`detail::bind`). Unqualified, ADL found `std::bind`
  for `Prop<std::string>` rvalues and silently dropped the binding.
- When an effect disposes other effects mid-flush (an `If` unmounting a branch), the queue must not
  keep dangling pointers. Make `dispose()` remove the node from the queue.
- Open in the design: `Resource<T>`, and `Store<T>` with
  `ELG_STORE(T, fields...)`.
