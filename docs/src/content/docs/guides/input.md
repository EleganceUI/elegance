---
title: Input, focus, and gestures
description: "Clicks and activation, hover, keyboard shortcuts, focus and Tab order, hit testing, and dropping files."
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

## Trackpad gestures

Pinching, turning two fingers, and a two-finger double tap (smart zoom) go to the innermost view
under the pointer that handles them, from `began` to `ended`:

```cpp
const State<float> zoom{1};
const State<float> start{1};
const State<float> angle{0};
Photo()
    .on_magnify([=](const MagnifyEvent& e) {
        if (e.phase == GesturePhase::began) {
            start = zoom();
        }
        zoom = std::clamp(start() * e.scale, 0.5f, 8.f); // scale is since the gesture began
    })
    .on_rotate([=](const RotateEvent& e) { angle = e.degrees; }) // clockwise, since it began
    .on_smart_magnify([=](Point) { zoom = zoom() == 1 ? 2.f : 1.f; });
```

Rotation and smart zoom come from macOS trackpads. Pinching also works where SDL reports it on
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
when it leaves. Change the delays for a subtree or for one handler:

```cpp
Grid().hover_delay({.enter = 0.3, .exit = 0.2});
Tile().on_hover_change(show_preview, HoverDelay{.enter = 0.5});
```

Controls' own highlights (buttons, menu buttons, toggles) follow the pointer at once; the hover
delay doesn't apply to them.

`.help("...")` shows a tooltip after the pointer rests on a view for a moment.

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

## Keyboard

Key events go to the focused view first, then bubble outward through the views around it until
one handles them.

```cpp
Editor().on_key_press(Key::escape, [=] { close(); });

Canvas(draw).focusable().on_key_press([=](const KeyEvent& e) {
    if (e.key == Key::s && has(e.modifiers, primary_modifier())) {
        save();
        return true;  // handled: stops here
    }
    return false;     // bubbles on
});
```

`primary_modifier()` is Ctrl on Windows and Linux and Cmd on macOS. `KeyEvent` also says whether a
key is auto-repeating.

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

## Custom event handling

For full control, write an `EventModifier` that creates an `EventHandler`. A handler receives
pointer, hover, scroll, key, focus and text input events for its view; returning
`{.consumed = true}` stops an event from bubbling further, and consuming a pointer `down`
captures the pointer until the matching `up`.

```cpp
struct DragHandler : EventHandler {
    explicit DragHandler(State<Point> position) : position(position) {}
    State<Point> position;
    EventResult pointer(const PointerEvent& e, Rect frame) override {
        if (e.kind == PointerEvent::Kind::down || e.kind == PointerEvent::Kind::move) {
            position = Point{e.location.x - frame.x, e.location.y - frame.y};
            return {.consumed = true};
        }
        return {};
    }
};

class Draggable : public EventModifier {
public:
    explicit Draggable(State<Point> position) : position_(position) {}
    std::unique_ptr<EventHandler> make_handler() const override { return std::make_unique<DragHandler>(position_); }

private:
    State<Point> position_;
};

const State<Point> cursor;
Canvas(draw).modifier(Draggable(cursor));
```

See [`<elg/events.h>`](../../reference/events/) for every event type.
