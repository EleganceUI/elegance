---
title: Menus and popups
description: "Context menus, menu buttons, submenus, menu pickers, popovers, and tooltips."
---

## Menus

A `Menu` is a panel of items: `MenuItem`s, `Submenu`s, `Separator`s, or any view.

```cpp
Menu(MenuItem("Rename", rename, {.shortcut = "F2"}),
     MenuItem("Duplicate", duplicate, {.image = ImageSource{.system_name = "copy"}}),
     Submenu("Move to", [=] { return Menu(MenuItem("Archive", archive), MenuItem("Trash", trash)); }),
     Separator(),
     MenuItem("Delete", remove, {.destructive = true}));
```

Choosing an item closes the menu, then runs its action. Items can show a shortcut hint, an icon, a
check mark (`.checked`, reactive), or the danger color (`.destructive`). Disable an item with
`.disabled(...)`.

Menus work with the keyboard: Up and Down move between items, Enter chooses, Esc closes, and Right
and Left enter and leave submenus.

## Context menus

`context_menu` opens a menu on right-click, or with Shift+F10 or the Menu key while the view has
focus. The builder runs each time the menu opens, so it can reflect the current state:

```cpp
Text(file.name).context_menu([=] {
    return Menu(MenuItem("Open", open),
                MenuItem("Pin", toggle_pin, {.checked = pinned}),
                Separator(),
                MenuItem("Delete", remove, {.destructive = true}));
});
```

## Menu buttons

A `MenuButton` opens a menu below itself:

```cpp
MenuButton(Label("Sort", {.system_name = "sort-ascending"}), [=] {
    return Menu(MenuItem("Name", [=] { sort = Sort::name; }, {.checked = [=] { return sort() == Sort::name; }}),
                MenuItem("Date", [=] { sort = Sort::date; }, {.checked = [=] { return sort() == Sort::date; }}));
});
```

For choosing one value from a list, `Picker` with `PickerStyle::menu` does this for you; see
[Controls](../controls/#picker).

## Popovers

`popover` shows a panel beside a view while a `Binding<bool>` is true, like SwiftUI's. Its content
can be anything, such as a small form:

```cpp
const State<bool> editing(false);

Button("Edit…", [=] { editing = true; })
    .popover(editing.binding(), [=] {
        return VStack(TextField("Name", name),
                      Button("Done", [=] { editing = false; }))
            .frame({.width = 240});
    });
```

Setting the binding to false closes it. Clicking outside, pressing Esc, or switching to another app
also closes it and sets the binding back to false. It opens centered below the view by default;
`{.opens = PopoverEdge::trailing}` (or `above`, `leading`) picks another side, and it flips when
there isn't room. Content is padded inside the panel; `{.padded = false}` lets it reach the edges.

A menu opened from inside a popover (a `MenuButton` in it) stacks on top of it, and choosing an item
closes both.

## Tooltips

```cpp
Button(Image("gear"), settings).help("Settings");
```

The tooltip appears after the pointer rests on the view for a moment.
