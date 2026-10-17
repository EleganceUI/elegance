---
title: Missing layout, components, and modifiers
description: Parts of the v1 catalog in the design that aren't built yet.
sidebar:
  order: 2
---

`design.md` §7, §9.4 and §13 list a v1 catalog. These parts of it don't exist yet.

## Tasks

### Layout
- [ ] `Grid` / `GridRow`
- [ ] `LazyVGrid` / `LazyHGrid` with `GridItem::fixed/flexible/adaptive`
- [ ] `ViewThatFits`, `GeometryReader`
- [ ] Alignment guides and custom alignment IDs; `.position`
- [ ] `HSplitView`
- [ ] Pixel snapping of layout results

### Structure and state
- [ ] `Switch(value).Case(...)`
- [ ] `Store<T>` with `ELG_STORE(T, fields...)`
- [ ] `Resource<T>` (loading / error / value)
- [ ] Reactive frame sizes and padding (more reactive arguments)

### Components
- [ ] `Link`, `Badge`
- [ ] `TextEditor` (a multi-line editor view); selectable `Text`
- [ ] `DatePicker`, `ColorPicker`, `Gauge`, `.redacted` skeletons
- [ ] `Table`, `OutlineGroup`, `DisclosureGroup`; List selection, sections, swipe actions
- [ ] `sheet`, `alert`, `confirmation_dialog`, `ComboBox`, `Toast`
- [ ] `NavigationStack` (push/pop with transitions), `NavigationSplitView`, `TabView`
- [ ] `MenuStyle`; style protocols for Slider and ProgressView

### Modifiers
- [ ] `mask(View)` (offscreen; the inspector should flag it)
- [ ] `blur`, `brightness`, `saturation`, `blend_mode`, `background_material`
- [ ] `rotation_effect`, `scale_effect`
- [ ] `on_appear`, `on_disappear`, `task(async fn)`, `on_change(value, fn)` (LifecycleModifier)
- [ ] `keyboard_shortcut`, `gesture(...)` with drag, long-press, magnify, rotate and composition (`simultaneously`, `sequenced`, `exclusively`), `GestureState<T>`
- [ ] Modifier fusion: merge adjacent built-in modifiers into one element and one quad

### Animation
- [ ] Velocity-preserving retargeting for springs
- [ ] `matched_geometry_effect`, keyframes, `PhaseAnimator`, gradient interpolation
