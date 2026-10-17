---
title: If and ForEach
description: Structural reactivity through fragments, keyed reuse, and per-item scopes.
sidebar:
  order: 7
---

Bodies run once, so anything that changes *which* views exist goes through `If` and `ForEach`.
Both are fragments: their children belong to the enclosing stack.

:::note[Depends on]
[Layout](../layout/) and [Reactive core](../reactive/).
:::

## Read first

- `design.md` §6 (Structural reactivity), M2
- `IfNode`, `ForEachNode`, `FragmentElement` in `src/view/view_node.h`; `src/view/fragment.cpp`

## Tasks

- [ ] `If(Prop<bool>, then_view, else_view = View())`. An empty branch takes no space
- [ ] `ForEach(items, key_fn, view_fn)`. Keys are integers, enums, or strings. Key functions go through `std::invoke`, so `&Item::id` works
- [ ] `FragmentElement::set_items`: keep children whose key **and** value are unchanged, rebuild changed ones, remove the rest, and keep the new order
- [ ] Mount each item or branch in its own `Scope`; unmounting destroys the scope (its effects and state) before the element
- [ ] `collect_layout_children`: inside a layout, the fragment's children *are* the layout's children (flattened); on its own, it stacks them vertically
- [ ] `notify_structure_changed()` so layouts rebuild their child lists, and set `tree_changed` so the dispatcher revalidates
- [ ] Removed children animating out (see [Animation](../animation/)): kept in `exiting_`, painted by the parent, not laid out, pruned when done

## Done when

- [ ] A todo list adds, removes, reorders and edits items, and only the touched rows rebuild (count body runs)
- [ ] A `State` created inside a row survives reordering, and is gone after removal
- [ ] Toggling an `If` that holds a focused text field clears focus safely

## Gotchas

- Each fragment child is wrapped for its transition, so a nested `If`/`ForEach` inside a ForEach
  item lays out on its own (vertically) instead of joining the outer stack. This is a known gap.
- Not built yet: `Switch(value).Case(...)`.
