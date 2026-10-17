---
title: Missing layout, components, and modifiers
description: "Proposed designs for the unbuilt parts of the v1 catalog, each placed on the existing protocols."
sidebar:
  order: 2
depends: [controls/popups-and-menus, controls/scrolling-and-lists, framework/animation, graphics/paths]
---

:::caution[Design, not built]
These are proposals. Each one is placed on protocols that exist, so none should need a new kind
of machinery unless noted.
:::

## Layout

- **`Grid` / `GridRow`**: a `CachedLayout` whose cache holds column widths and row heights.
  Measure each cell with the proposal split evenly, take the max per column and row, then
  distribute leftover space to flexible columns. `GridRow` is a layout value marking row
  membership.
- **`LazyVGrid` / `LazyHGrid`**: a lazy stack whose "items" are rows of N cells, with N from
  `GridItem::fixed/flexible/adaptive` and the cross size. Reuses the position-keyed size cache.
- **`ViewThatFits(a, b, c)`**: a layout that measures children with the proposal in order and
  places the first whose size fits, hiding the others (they stay mounted but aren't placed or
  painted).
- **`GeometryReader(fn(Size))`**: an element that takes the proposal and builds its content from
  the size it was given. It rebuilds through a one-item ForEach keyed by size, or better, passes
  the size as a `State` so the body runs once.
- **Alignment guides**: give `LayoutChild` an `alignment(guide)` query that defaults to
  leading/center/trailing fractions and can be overridden with a modifier; stacks align by it.
- **Pixel snapping**: round frames to device pixels in `place`, keeping sizes consistent.

## State

- **`Store<T>`**: per-field signals for a struct. Until reflection, `ELG_STORE(T, a, b, c)`
  generates a wrapper with a `State` per field and accessors.
- **`Resource<T>`**: a fetch-like handle (`loading`, `error`, `value` as States) around an async
  function run on a worker, with cancellation tied to the scope.
- **`Switch(value).Case(v, view)...`**: an If generalized to N branches; one fragment keyed by
  the matching case.

## Lifecycle modifiers

A new `LifecycleModifier` protocol lowers to an element that calls hooks:
- `on_appear` after the first placement, `on_disappear` when its scope ends;
- `task(fn)`: start async work at appear, cancel at disappear;
- `on_change(value, fn)`: an effect comparing old and new values.

## Presentations

- **`sheet(isPresented, content)`**: a modal panel in a **top layer of the same window**, with a
  dimmed backdrop, a focus trap (Tab stays inside), and Esc to dismiss. It needs focus scopes in
  the `FocusManager`.
- **`alert` / `confirmation_dialog`**: sheets with a fixed layout of title, message and buttons
  with roles (cancel, destructive).
- **`Toast`**: a top-layer view with a timer and a transition.
- **`ComboBox`**: a TextField with a popover list, using `TextFieldOptions::on_key` for
  Up/Down/Enter.

## Navigation

- **`NavigationStack(path)`**: a ZStack over a list of destination views keyed by path entries.
  Pushing appends with a `move(trailing)` transition; popping removes.
- **`NavigationSplitView`**: an HStack of sidebar and detail with a draggable divider (`HSplitView`).
- **`TabView`**: a segmented header plus a one-item ForEach keyed by the selected tab, so each tab
  mounts on demand (or keeps all mounted and hides inactive ones, if state must survive).

## Effects

- **`mask(View)`**: render the content into a layer and the mask view into a second layer, then
  composite the content with destination-in by the mask's alpha. It's the costliest effect, and the
  inspector should flag it.
- **`blur`**: a layer plus the existing separable Gaussian on all four channels.
- **`background_material`**: blur what's **behind** the view, which needs a copy of the target
  so far. This one needs new renderer support (a "backdrop" pass).
- **`rotation_effect` / `scale_effect`**: a real transform on the element, which the display
  list doesn't have yet. Commands carry translation and scale today; rotation needs a transform in
  instances and in hit testing.

## Gestures

A `Gesture` protocol inside `EventModifier`: recognizers (tap, long press, drag, magnify,
rotate) as state machines over pointer events, with `GestureState<T>` reset when a gesture ends,
and composition (`simultaneously`, `sequenced`, `exclusively`) deciding which recognizer gets
events.

## Modifier fusion

Merge runs of built-in modifiers (padding, background, clip_shape, shadow) into one element that
emits one quad, by recognizing their `ModifierKind`s at lowering. User modifiers act as barriers,
which stays correct and just doesn't fuse.

## Animation

- **Velocity-preserving retargeting**: springs keep their velocity when the target changes.
  Store velocity in `Animated` and solve the spring from (position, velocity).
- **`matched_geometry_effect(id, namespace)`**: elements sharing an id hand their frame over during
  an animated update, so a view appears to move from one place to another.
