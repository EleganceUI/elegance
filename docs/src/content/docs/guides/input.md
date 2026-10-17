---
title: Input, focus, and gestures
description: "Taps, hover, keyboard shortcuts, focus and Tab order, hit testing, and dropping files."
---

Controls handle their own input. For everything else, event modifiers attach handlers to any
view.

## Taps

```cpp
Card().on_tap_gesture([=] { open(card_id); });
Thumbnail().on_tap_gesture([=] { zoom(); }, 2); // a double tap
```

A tap is a press and release inside the view. When tap gestures are nested, the innermost one
under the pointer wins.

Views made clickable with gestures should tell screen readers what they are; see
[Accessibility](../accessibility/).

## Hover

`on_hover` reports when the pointer has rested on a view, and when it leaves:

```cpp
const State<bool> hovered(false);
Row()
    .background([=] { return hovered() ? Color::hex(0x7AA2F7).opacity(0.1f) : Color::clear; })
    .on_hover([=](bool inside) { hovered = inside; });
```

By default the pointer must rest for 0.1 s before a view counts as hovered, so sweeping across a
list doesn't light up every row. Change the delays for a subtree or for one handler:

```cpp
Toolbar().hover_delay({.enter = 0, .exit = 0.2});
Tile().on_hover(show_preview, HoverDelay{.enter = 0.5});
```

`.help("...")` shows a tooltip after the pointer rests on a view for a moment.

## Hit testing

A view is hit where its content draws: shapes by their outline, text by its frame. Two modifiers
change that:

```cpp
HStack(Image("star"), Spacer(), Text("Rate")).content_shape(Rectangle()).on_tap_gesture(rate); // the gaps count too
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
