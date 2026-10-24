---
title: Accessibility
description: "Element semantics, how modifiers attach to elements, building the tree on demand, incremental updates, and actions."
sidebar:
  order: 2
depends: [controls/basic-controls, text/text-input, controls/scrolling-and-lists]
---

## What it's for

Screen readers and other assistive technology need a tree of what's on screen: roles, names,
values, states, and actions they can perform. Elegance draws everything itself, so it must
describe everything itself. It hands that description to **AccessKit**, which speaks each OS's
accessibility API.

## How it works

### Elements describe themselves

`virtual void Element::accessibility(AccessibilityInfo&) const` fills in:

- `role`: window, group, label, heading, button, link, toggle (switch), checkbox, slider,
  stepper, progress, text field, password field, image, video, list, list item, scroll view, tab,
  menu, menu item, pop-up button;
- `label` (the name), `value`, `hint`, `placeholder`;
- states: hidden, combine, disabled, toggled, expanded, selected;
- a numeric value with min, max and step;
- the actions it supports.

An element with no role is **transparent**: its children are attached to its parent in the tree.
Each control knows its semantics. Text is a label (empty text and icons are hidden), a button's
name combines the text inside it, a slider exposes its value and range, a text field exposes its
text (a secure field never does), and so on.

### Modifiers

`.accessibility_label/value/hint/hidden/role/element/action(...)` don't create elements of their
own. While building the tree, their settings travel **down** to the element they describe: the
element they wrap, or the nearest one inside it with a role (a Toggle's role lands on its button).
If they wrap several children and none takes them, they form a **group** around them.
`.accessibility_element()` combines a subtree into one node, labeled with the text inside it in
order, and hides the children.

### Building the tree on demand

The adapter is AccessKit's **Windows subclassing adapter**: it hooks the window's `WM_GETOBJECT`
and does nothing until a client (Narrator, NVDA, an automation tool) asks.

- **First request**: AccessKit calls back synchronously on the UI thread. Elegance builds the
  whole tree (walk the elements, apply the modifier rules, assign ids), converts every node to an
  AccessKit node, and remembers what it sent. The adapter is now **active**.
- **Ids**: each element's `accessibility_id()` is a process-unique number assigned on first use.
  The window root is 1.
- **Bounds**: window points × scale.
- **Focus**: the focused element's node, or the nearest ancestor that has one.

### Updating

After each frame, while active (and coalesced when frames come quickly):

1. Rebuild the tree from current values. No bookkeeping keeps it in sync; it's just rebuilt.
2. Compare each node with the one **last sent** under the same id.
3. Send only nodes that changed, plus focus if it moved. A scale change resends everything.

Converting every node of a large window each frame cost milliseconds; diffing on Elegance's side
keeps updates proportional to what changed. With no client, none of this runs, and idle CPU stays
0.

### Actions

Requests (click, focus, increment/decrement, set value, set numeric value, scroll into view)
arrive on the **screen reader's thread**. They're copied and posted to the UI thread. There, the
target is found by id **through the window's current callbacks**, so a request for a window that
closed meanwhile finds nothing. The element's `accessibility_action` performs it: buttons
activate, sliders step or set, fields replace their text. Focus goes through the
`FocusManager`. Scroll into view calls `reveal()` up the ancestors.

## Rules

- Elements without a role add no node.
- Never expose a secure field's text.
- Zero work while no client is listening.
- Actions run on the UI thread, never on the client's thread.

## Edge cases

- **Popups and menus** are separate windows and aren't yet exposed as their own trees with menu
  roles.
- **Text fields** don't expose caret and selection (text ranges) yet.
- **macOS and Linux** need AccessKit's NSAccessibility and AT-SPI adapters wired to native
  handles.
- **Verification** so far is through Windows UI Automation from a script; testing by a person with
  Narrator and NVDA is still to do.
