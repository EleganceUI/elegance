---
title: Input, focus, and gestures
description: "Clicks and activation, gestures, hover, keyboard shortcuts, focus and Tab order, hit testing, and dropping files."
---

Controls handle their own input. For everything else, event modifiers attach handlers to any
view.

## Clicks and activation

`on_click` runs on a press and release inside the view. It can take the click's details: where it
was (in the view's own coordinates), the button, the modifiers held and the click count:

```cpp
Row().on_click(select).on_double_click(open);
Canvas(draw).on_click([=](const ClickEvent& click) { add_point(click.location); });
Item().on_click([=](const ClickEvent& click) { extend_selection(id, click.modifiers); });
Tab().on_click(close_tab, {.button = MouseButton::middle});
```

When click handlers are nested, the innermost one for that button wins. A double-click handler
lets first clicks through to an outer single-click one.

A click is only the pointer. For something that's a control (a tile that opens, a custom
switch), use `on_activate`. It runs on a click, on Space or Enter while the view has focus, and
on a screen reader's default action. It makes the view focusable, and `.disabled()` turns it
off:

```cpp
AlbumTile(album).on_activate([=] { open(album); }).accessibility_label(album.title);
```

Give custom controls a role and label too; see [Accessibility](../accessibility/).

## Gestures

`recognize` attaches a gesture, built up with the callbacks it needs:

```cpp
const State<Point> offset;
const State<Point> start;
Card()
    .offset([=] { return offset().x; }, [=] { return offset().y; })
    .recognize(DragGesture({.minimum_distance = 8})
                   .on_began([=] { start = offset.peek(); })
                   .on_changed([=](const DragEvent& e) {
                       offset = Point{start().x + e.translation.x, start().y + e.translation.y};
                   })
                   .on_ended([=](const DragEvent& e) { settle(e.velocity); }))
    .on_click(open);

Thumbnail(photo).recognize(
    LongPressGesture(show_details, {.duration = 0.4, .pressing = [=](bool held) { pressed = held; }}));
```

`DragEvent` has where the pointer is and where the press went down (in the view's coordinates),
the translation since the press, the velocity in points per second, the button and the modifiers.

A drag or long press watches presses without taking them. When it recognizes one (the pointer
moves `minimum_distance`, or the press is held for `duration`), it takes the press over, and a
button or `on_click` it started on doesn't fire. So the card above opens on a click and moves on
a drag. Controls that track the pointer themselves, like sliders and text fields, keep their
presses: dragging a slider in a draggable card moves the slider.

`on_drag(action)` and `on_long_press(action)` are shorthands for a gesture with only
`on_changed` or the action.

### When gestures meet

When several gestures could take a press, the innermost one that recognizes it wins. A drag or
long press can be told otherwise with `GestureOptions`, the second argument of `recognize`:

```cpp
// A row's swipe goes before the drags of what's in it, and over its sliders.
Row(item).recognize(DragGesture().on_changed(swipe), {.priority = GesturePriority::high});

// Tracks the press beside whatever has it: nothing is cancelled.
Card().recognize(DragGesture().on_changed(tilt), {.simultaneous = true});
```

- `.priority = GesturePriority::high` offers the press to the gesture before the gestures inside
  its view, and lets it take presses from controls that keep theirs, like sliders.
- `.simultaneous = true` never takes the press. It recognizes alongside the gesture or control
  that has it, and neither cancels the other.

For hold-then-drag, as when reordering by touch, give the drag a `hold`:

```cpp
Tile(photo).recognize(DragGesture({.hold = 0.4})
                          .on_began([=] { lifted = true; })
                          .on_changed([=](const DragEvent& e) { offset = e.translation; })
                          .on_ended([=](const DragEvent&) { lifted = false; drop(); }));
```

The press has to stay put (within `hold_tolerance`, 10 points) for `hold` seconds. Then the drag
begins at once, with no translation, so a lifted look can show before anything moves, and it
follows the pointer from there. A press that moves sooner is left to the rest: a scroll view
scrolls, and a click is still a click.

### Trackpad gestures

Pinching, turning two fingers, and a two-finger double tap go to the innermost view
under the pointer that handles them, from `began` to `ended`:

```cpp
const State<float> zoom{1};
const State<float> start{1};
const State<float> angle{0};
Photo()
    .recognize(MagnifyGesture()
                   .on_began([=] { start = zoom(); })
                   // scale is since the gesture began
                   .on_changed([=](const MagnifyEvent& e) { zoom = std::clamp(start() * e.scale, 0.5f, 8.f); }))
    .on_rotate([=](const RotateEvent& e) { angle = e.degrees; }) // clockwise, since it began
    .on_two_finger_double_tap([=](Point) { zoom = zoom() == 1 ? 2.f : 1.f; });
```

Rotation and two-finger double taps come from macOS trackpads. Pinching also works where SDL reports it on
other platforms.

## Hover

There are two kinds of hover. `on_pointer_enter` and `on_pointer_leave` fire the moment the
pointer crosses the view's hit area. Use them for highlights, which should match what's under the
pointer exactly:

```cpp
const State<bool> hovered(false);
Row()
    .background([=] { return hovered() ? Color::hex(0x7AA2F7).opacity(0.1f) : Color::clear; })
    .on_pointer_enter([=] { hovered = true; })
    .on_pointer_leave([=] { hovered = false; });
```

`on_hover_change` is hover *intent*: it reports true only once the pointer has rested on the view
(0.1 s by default), so sweeping across a grid doesn't start a preview on every tile, and false
when it leaves. Change the delays with `.hover_delay`, for everything inside it:

```cpp
Grid().hover_delay({.enter = 0.3, .exit = 0.2});
Tile().on_hover_change(show_preview).hover_delay({.enter = 0.5}); // just this handler
```

Controls' own highlights (buttons, menu buttons, toggles) follow the pointer at once; the hover
delay doesn't apply to them.

`.tooltip("...")` shows a tooltip after the pointer rests on a view for a moment.

## Pointer style

`.pointer_style()` sets the pointer's shape over a view, like CSS's `cursor`. The innermost view
under the pointer with a style decides; with none, it's the arrow. Text fields show the I-beam
and split view dividers the resize arrows on their own.

```cpp
Card().on_activate(open).pointer_style(PointerStyle::pointer); // a hand: this opens something
Canvas(draw).pointer_style(PointerStyle::crosshair);
Content().pointer_style([=] { return busy() ? PointerStyle::progress : PointerStyle::automatic; });
```

The shape follows the pointer and the views: when a view goes away or moves out from under a
still pointer, its style goes with it. `PointerStyle::automatic` leaves the decision to the views
around. A custom `EventHandler` whose shape depends on where the pointer is (a canvas's resize
handles) overrides `pointer_style(Point location)` instead.

## Hit testing

A view is hit where its content draws: shapes by their outline, text by its frame. Two modifiers
change that:

```cpp
HStack(Image("star"), Spacer(), Text("Rate")).content_shape(Rectangle()).on_click(rate); // the gaps count too
Overlay().allows_hit_testing(false); // clicks go through to what's behind
```

## Disabled views

`.disabled()` switches off the handlers inside it, not only controls: clicks, double clicks,
gestures, keys, text input, focus, context menus and drops. A press on a disabled view goes to
the views around it, as if it weren't there:

```cpp
Card(item).on_click(open).disabled([=] { return item.locked; }) // clicks reach what's around it
```

Hover still reaches it, so a tooltip can say why it's disabled, and scrolling still works, so a
disabled form can still be read. A window drag area keeps moving the window.

When a view becomes disabled partway through, what it had going stops: a drag or long press gets
`on_cancelled` (not `on_ended`), and if it had focus, focus leaves it.

## Keyboard

Key events go to the focused view first, then bubble outward through the views around it until
one handles them.

```cpp
Editor().on_key_press(Key::escape, [=] { close(); });

Canvas(draw).focusable().on_key([=](const KeyEvent& e) {
    if (e.kind == KeyEvent::Kind::down && e.key == Key::s && has(e.modifiers, primary_modifier())) {
        save();
        return true;  // handled: stops here
    }
    return false;     // bubbles on
});
```

`on_key_press` runs on key downs; `on_key` sees downs and ups. `primary_modifier()` is Ctrl on
Windows and Linux and Cmd on macOS. `KeyEvent` also says whether a key is auto-repeating.

Typed text is separate from keys: `on_text_input` makes a view focusable and receives what's typed
into it, including an IME's composition in progress.

```cpp
Canvas(draw).on_text_input([=](const TextInputEvent& e) {
    if (e.kind == TextInputEvent::Kind::commit) {
        insert(e.text);
    }
    return true;
});
```

## Focus

Controls take keyboard focus when clicked, and Tab and Shift+Tab move focus through them in order.
Focused controls show a focus ring. Make other views focusable, or keep a subtree out of the Tab
order:

```cpp
Canvas(draw).focusable();
Toolbar().focusable(false); // its buttons never take focus
```

Bind focus to state to move it from code, or to react to it:

```cpp
const State<bool> search_focused(false);
TextField("Search", query).focused(search_focused);
Button("Find", [=] { search_focused = true; });
```

Inside a scroll view, focusing a view scrolls it into sight.

## Dropping files and text

Accept files or text dragged from other apps, such as Explorer or a browser:

```cpp
const State<bool> targeted(false);
DropZone()
    .on_drop({.files = [=](const std::vector<std::filesystem::path>& files, Point where) {
                  import(files);
                  return true; // accepted
              },
              .text = [=](const std::string& text, Point) { paste(text); return true; },
              .targeted = [=](bool over) { targeted = over; }});
```

The innermost view under the pointer that takes that kind of content gets it. `targeted` reports
when a drag that the view can take is over it, for a highlight.

## Raw pointer and scroll events

`on_pointer` and `on_scroll` hand over the events themselves, located in the view's own
coordinates (`window_location` has the window's). Return true to consume an event; consuming a
pointer `down` captures the pointer, so the moves and the `up` come to the view wherever the
pointer goes:

```cpp
const State<Point> cursor;
Canvas(draw).on_pointer([=](const PointerEvent& e) {
    if (e.kind == PointerEvent::Kind::down || e.kind == PointerEvent::Kind::move) {
        cursor = e.location;
        return true;
    }
    return false;
});

Canvas(draw).on_scroll([=](const ScrollEvent& e) {
    if (!has(e.modifiers, primary_modifier())) {
        return false; // the scroll view around it scrolls
    }
    zoom_by(e.dy, e.location);
    return true;
});
```

For anything more, such as one handler that takes several kinds of events, write an
`EventModifier` that creates an `EventHandler`. A handler receives pointer, hover, scroll, key,
focus and text input events for its view, in window coordinates along with the view's frame;
returning `{.consumed = true}` stops an event from bubbling further.

See [`<elg/events.h>`](../../reference/events/) for every event type.
