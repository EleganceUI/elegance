---
title: Accessibility
description: Element semantics, accessibility modifiers, and an on-demand AccessKit tree with actions.
sidebar:
  order: 2
---

Every element can describe itself. When a screen reader asks, the whole tree is rebuilt from
current values after each frame and AccessKit diffs it. With no screen reader listening, nothing
runs at all.

:::note[Depends on]
Every control you want exposed. Do this after [Controls](../../controls/basic-controls/), but
design the `accessibility()` hook into `Element` from the start.
:::

## Read first

- `design.md` §12 (Accessibility)
- `src/view/accessibility.*`, `src/platform/accessibility_adapter.*`, `AccessRole` / `AccessibilityInfo` in `src/view/element.h`, `tests/accessibility_tests.cpp`
- AccessKit's C API (`accesskit-c`)

## Tasks

### Build
- [ ] CMake downloads the official prebuilt `accesskit-c` release (pinned version and SHA-256) and links it statically behind `ELG_ACCESSIBILITY` (ON). No Rust toolchain needed

### Semantics
- [ ] `AccessibilityInfo`: role, label, value, hint, placeholder, hidden, combine, disabled/toggled/expanded/selected states, numeric value and range, supported actions
- [ ] Elements without a role are transparent: their children join their parent's
- [ ] Text is a label (icons and empty text hidden); buttons combine their content's text into their name
- [ ] Toggle is a switch with its state; MenuButton and menu Picker are pop-up buttons
- [ ] Sliders: value, range, step, increment/decrement, set value. ProgressView: its fraction
- [ ] Text fields: text, placeholder, set value. **Never** a secure field's text
- [ ] Image, video and scroll view roles; stepper buttons named "Decrease" / "Increase"

### Modifiers
- [ ] `.accessibility_label`, `_value`, `_hint` (reactive), `_hidden`, `_role`, `_element` (combine children into one element), `_action`
- [ ] They apply to the element they wrap, or the nearest one inside with a role; around several children they form a group

### Adapter
- [ ] AccessKit's Windows subclassing adapter hooks `WM_GETOBJECT` and stays dormant until a screen reader asks
- [ ] Once active, rebuild the tree after each frame (coalesced while frames come quickly), compare each node with what was last sent (by id), and send only changed nodes and a changed focus. A scale change resends everything. Rebuilding every AccessKit node of a large window each frame cost milliseconds
- [ ] Node ids: per-element process-unique ids (window root is 1); bounds in window points × scale
- [ ] Focus: the focused element's node, or the nearest one around it
- [ ] Action requests arrive on the screen reader's thread; copy and post them to the UI thread: click, focus (through `FocusManager`), increment/decrement, set value/number, scroll into view (`reveal()` up the ancestors)

## Done when

- [ ] A UI Automation script against the sandbox reads names, roles, values, toggle and range states correctly
- [ ] Invoke, Toggle, RangeValue.SetValue, Value.SetValue on a field, and SetFocus all act on the app
- [ ] Idle CPU stays 0 with no screen reader running
- [ ] A person tests with Narrator and NVDA

## Gotchas

- Follow-ups: macOS (NSAccessibility) and Linux (AT-SPI) adapters (AccessKit has them; they need
  the native window handles); popups and menus as their own trees; live regions; text selection
  and caret for fields; list/list-item roles for `List`; headings for large titles; reduced
  motion and high contrast from system settings.
