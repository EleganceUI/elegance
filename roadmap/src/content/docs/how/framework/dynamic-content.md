---
title: If and ForEach
description: "Fragments: how views appear and disappear after mount, keyed reuse, and per-item scopes."
sidebar:
  order: 7
depends: [framework/layout, framework/input-and-focus]
---

## What it's for

Bodies run once, so a body can't decide "show this only sometimes" with plain C++ control flow.
`If` and `ForEach` are views whose children change over time. They mount and unmount subtrees as
state changes, and keep everything else in place.

## How it works

### Fragments

Both lower to a `FragmentElement`, which holds an ordered list of **children**, each:

```
{ key, value, element (wrapped in a TransitionElement), scope }
```

A fragment is not a container in layout. Its `collect_layout_children` returns its current
children's elements, so **inside a stack, a fragment's children are the stack's children**: they
take part in spacing, priorities and alignment as if written there directly. On its own (not
directly in a layout), a fragment stacks its children vertically.

### Driving it

Lowering creates one effect per fragment that reads the condition or item list and calls
`set_items(items)`:

- **`If(condition, then, else)`** produces zero or one item with a fixed key per branch.
  Re-evaluating to the same branch keeps its views. An empty branch (`View()`) mounts nothing, so
  stacks don't put spacing around it.
- **`ForEach(collection, key_fn, view_fn)`** produces one item per element, with the key from
  `key_fn` (through `std::invoke`, so `&Item::id` works). Keys are integers, enums or strings.

The effect **tracks only the condition or collection**. Building the children runs in
`untrack`, so state a child's body reads directly doesn't subscribe the fragment.

### Reconciling

`set_items(new_items)`:

1. For each new item, find an existing child with the same key. If its **value** is also equal,
   keep the child (element, scope and state). Otherwise mount a new one.
2. Order children as the new list orders them.
3. Children not kept **exit**. If the change happened in an animated update and the child's
   transition has something to animate, it moves to `exiting_` and keeps painting until its
   transition ends. Otherwise its **scope is destroyed first** (ending its effects), then its
   element.
4. Tell the parent layout its children changed (`notify_structure_changed`), and raise
   `tree_changed` so the event dispatcher revalidates.

Mounting an item: create a `Scope`, make it current, lower the item's view inside it, wrap the
element in a `TransitionElement` (the child's own `.transition(...)`, else a fade). The first
`set_items` call is the initial mount and never animates.

Exiting children are not laid out. The parent layout paints them at their last frames (through
`collect_exiting`), and `prune_exiting` destroys them when their exit finishes. While any exist,
the fragment `always_arranges`, so pruning happens.

### Why per-item scopes

Everything a row creates (its `State`, its effects, its reactive arguments) lives in the row's
scope. Removing the row ends all of it deterministically. That's the
[hot-reload rule](../../tools/hot-reload/) of scoped ownership applied to lists. Reordering keeps
scopes, so a row's local state survives a sort.

## Rules

- Keys identify items; equal key and equal value means "keep".
- A child's scope is destroyed before its element.
- The fragment's effect never subscribes to what children read.

## Edge cases

- **Duplicate keys**: log a warning once, and rebuild items with a repeated key on every change.
  It stays correct, just slower, and the warning points at the bug.
- **Values without `==`** are always rebuilt when the list changes.
- **Nested fragments inside a ForEach item**: each item is wrapped for its transition, so a
  nested `If`/`ForEach` lays out on its own (vertically) instead of joining the outer stack. This
  is a known gap; the fix is to let transitions wrap flattened children.
- **Lazy stacks** reuse the same item model but build only visible items (see
  [Scrolling and lists](../../controls/scrolling-and-lists/)).
