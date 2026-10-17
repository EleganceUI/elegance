---
title: Reactive core
description: "The dependency graph behind State, how updates propagate glitch-free, and who owns effects."
sidebar:
  order: 1
depends: [foundations/core]
---

## What it's for

Elegence runs a view's body **once**. Everything dynamic after that comes from this graph: state
you write, values derived from it, and effects that push new values into elements. A change
re-runs exactly the code that read the changed state, once, with consistent inputs. No view tree
is re-evaluated or diffed.

## How it works

### Three kinds of node

| Node | Holds | Runs |
|---|---|---|
| **Signal** (`State<T>`) | A value | Never; it's written |
| **Computed** (`Computed<T>`) | A cached derived value | Lazily, when read and out of date |
| **Effect** | Nothing | Eagerly, after a change, in a flush |

Each node keeps `sources_` (what it read last time it ran) and `observers_` (who read it). The
graph is rebuilt on every run: before a computed or effect runs, it unlinks all its sources, and
every read during the run links again. Dependencies therefore always match the code path actually
taken; a branch that stopped reading something stops depending on it.

### Tracking

A global "current observer" pointer is set while a computed or effect runs. A read (`state()`)
calls `track()`, which:

1. brings the node itself up to date if it's a computed,
2. links it to the current observer (once, even if read several times).

`peek()` and `untrack(fn)` read without linking. Untracked code clears the current observer and
restores it afterwards; effects created inside still track their own reads, because each effect
makes itself the observer while it runs.

### Push dirty, pull values

Each node is `clean`, `check` (an ancestor may have changed) or `dirty` (a direct source changed).

**On write**, `notify_changed()` marks direct observers `dirty`, and they mark *their*
observers `check`, recursively. Marking stops at nodes already at that level or higher. Any
effect that gets marked is **queued**. Nothing is recomputed yet.

**On read or flush**, `update()` resolves a node:

- `check`: update each source in order. A computed source that recomputes **to a different
  value** marks this node `dirty`. If none did, the node becomes `clean` without running.
- `dirty`: run it.

A computed whose new value `==` its old one doesn't dirty its observers, so equal results stop
the propagation. This is the "Reactively" algorithm, and it gives two guarantees:

- **No redundant work**: every node runs at most once per change.
- **No glitches**: in a diamond (`a → b`, `a → c`, `b + c → d`), `d` runs once and sees updated
  `b` and `c`, never one old and one new.

### Effects and flushing

Effects queue in order. `flush_effects()` runs the queue; the framework calls it once per loop
iteration after events, and again before layout. Writes inside an effect queue more effects in
the same flush. Rules:

- **Batching**: `batch(fn)` defers the flush to the end of the outermost batch, so several writes
  run dependents once.
- **Priority effects** go to the front of the queue. Implicit animations use them: their trigger
  must notice "the value changed" before the effects that apply the changed properties run (see
  [Animation](../animation/)).
- **Cycle guard**: after 100,000 runs in one flush, the queue is cleared and a `UsageError` says
  an effect probably writes state it reads.
- **Transactions**: `with_animation` sets a current animation while writes happen. A write stamps
  each effect it queues with that animation, and the effect runs with `current_animation()` set
  to it, so the properties it applies animate.

### Ownership: scopes

Effects are owned by a `Scope`. `Scope::Use` makes a scope current while views are built;
`effect()` (and every reactive argument) adopts its effect into the current scope. Destroying a
scope disposes its effects: they unlink from the graph and leave the queue, even if already
queued. With no current scope, `effect()` throws, because an effect without an owner would live
forever.

The framework gives each window a scope, and each If/ForEach item its own nested scope, so
removing views ends exactly their effects.

Signals and computeds are owned by their handles (`shared_ptr`). A `State` captured in a lambda
lives as long as the lambda.

### Threading

Reactive state belongs to the UI thread while an App runs. `check_ui_thread()` throws
`UsageError` on writes and effect starts from other threads. Before `run()` (tests, setup), any
thread may use it. Background work reaches state through `post_ui_task`.

### The public types

- **`State<T>`**: a handle to a signal. Copies share the value, so lambdas capture it by value
  and still write it. The constructor is `explicit`, so `count = 1` means "set" rather than
  "replace with a new State". `set` skips notification when the value is equal.
- **`Binding<T>`**: a get/set pair, made from a `State` or by hand. Child components take
  Bindings to change state they don't own.
- **`Computed<T>`** / `computed(fn)`, **`effect(fn)`**, **`batch`**, **`untrack`**.
- **`Prop<T>`**, the reactive argument: one of a static `T`, a closure returning `T` (which
  covers States, Bindings and Computeds, since they're callable), or a closure taking
  `const EnvironmentValues&`. A `ThemeColor` becomes the environment kind. The framework's
  `bind(prop, context, apply)` applies a static value once, or wraps the closure in one effect
  that calls `apply` with each new value.

## Rules

- Bodies run once; only effects re-run.
- Every effect has an owning scope.
- A node runs at most once per change and never sees half-updated inputs.
- State is touched only on the UI thread.

## Edge cases

- **An effect disposing others mid-flush** (an `If` switching branches): disposal removes nodes
  from the queue, so the queue never holds dangling pointers.
- **Exceptions inside a computed or effect**: the observer pointer is restored and the node
  marked clean before rethrowing, so the graph stays consistent.
- **Types without `==`**: every write notifies, and computeds always propagate.
