---
title: Menus and popups
description: "Context menus, menu buttons, submenus, menu pickers, popovers, floating content, and tooltips."
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

Its options place the menu like [floating content](#floating-content) (above a button near the
bottom of the window, say), hide the chevron, or give it its own motion:

```cpp
MenuButton("Attach", attach_menu, {.placement = {.edge = FloatingEdge::top, .spacing = 4},
                                   .shows_indicator = false});
```

For choosing one value from a list, `Picker` with `MenuPickerStyle` does this for you; see
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
`{.placement = {.edge = FloatingEdge::trailing}}` (or `top`, `leading`) picks another side, and it
flips when there isn't room. Content is padded inside the panel; `{.padded = false}` lets it reach
the edges. A popover is `floating` (below) in a panel.

A menu opened from inside a popover (a `MenuButton` in it) stacks on top of it, and choosing an item
closes both.

## Floating content

`floating` is what popovers are made of: content above everything by a view, while a binding is
true, with no panel or shadow of its own. Its options say where it goes, what closes it, and
whether it may leave the window:

```cpp
Avatar(user).floating(card_shown.binding(), [=] { return ProfileCard(user); },
                      {.placement = {.edge = FloatingEdge::trailing, .alignment = FloatingAlignment::center,
                                     .spacing = 8},
                       .boundary = FloatingBoundary::window,
                       .dismiss = {.outside_click = false}});
```

- `placement`: the `edge` of the view it opens on (flipping when there isn't room), its
  `alignment` along that edge, the `spacing` from the view, and an `offset`. Or an explicit
  `position`, in the view's coordinates.
- `boundary`: `screen` (the default) puts it in a popup window of its own, so it can extend past
  the app's window, kept on the display. `window` draws it inside the app's window, above the
  content, and keeps it there. On Wayland the compositor can move or limit popup windows, so use
  `window` where exact placement matters.
- `dismiss`: by default a press outside it or Esc closes it (and sets the binding back to false).
  While it's open, presses elsewhere only close it. With `.outside_click = false` the rest of the
  window stays usable, and it closes when the binding goes false, or on Esc.

Its size follows its content as that changes, and it follows the view when the view moves.

### Opening and closing

Popups appear and disappear at once by default. A `PopupMotion` makes them come and go with a
transition, run with an animation; it plays both ways, and a popup stays on screen (without taking
input) until it has animated out:

```cpp
const PopupMotion grow(Transition::opacity().combined(Transition::scale(0.95f, Alignment::top)),
                       Animation::ease_out(0.15));
Button("Details", [=] { showing = true; }).popover(showing.binding(), details, {.motion = grow});
```

`floating`, `popover` and `tooltip` take `.motion` in their options. Without one, popovers,
tooltips and menus use their style's `motion()`, which a custom style can override to animate
every popup it draws.

## Tooltips

```cpp
Button(Image("gear"), settings).tooltip("Settings");
Avatar(user).tooltip([=] { return HStack(Image("user"), Text(user.name)); },
                     {.placement = {.edge = FloatingEdge::trailing, .spacing = 6}, .delay = 0.3});
```

A tooltip appears after the pointer rests on the view for a moment (`delay`, 0.6 seconds by
default), and goes when it leaves or presses. `tooltip("text")` is a tooltip with that text. Its
`placement` and `boundary` work as for [floating content](#floating-content); an in-window tooltip
lets the pointer through to what's under it.

Popovers, tooltips and menus draw their panels with the environment's `PopoverStyle`,
`TooltipStyle` and `MenuStyle`; see [Styles](../styles/#popover-tooltip-and-menu-styles).
