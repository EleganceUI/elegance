---
title: Controls
description: "Buttons, toggles, text fields, sliders, steppers, pickers and progress views."
---

Controls own their behavior: hover and press states, keyboard focus, keyboard activation and
screen reader support. Their look comes from [styles](../styles/) and the environment's tint.
Controls that edit a value take a [`Binding`](../state/#binding); a `State` converts to one.

## Button

```cpp
Button("Save", save);
Button(Label("Share", {.system_name = "share-network"}), share);
Button(Image("trash"), remove).accessibility_label("Delete");
Button(Text([=] { return playing() ? "Pause" : "Play"; }), toggle); // a reactive label
```

The label is any view. A button runs its action when clicked, or when activated with Enter or
Space while it has focus.

Three styles are built in. `ProminentButtonStyle` (filled with the tint) is the default:

```cpp
Button("Cancel", cancel).button_style(BorderedButtonStyle{});
Button("Learn more", learn).button_style(PlainButtonStyle{});

HStack(Button("A", a), Button("B", b)).button_style(BorderedButtonStyle{}); // for everything inside
```

Change the color with `.tint(...)`, and disable a button (or everything in a view) with
`.disabled(...)`:

```cpp
Button("Delete", remove).tint(Color::hex(0xFF453A));
Button("Submit", submit).disabled([=] { return !form_valid(); });
```

## Toggle

An on/off switch bound to a `bool`. Click, Space or Enter flips it.

```cpp
const State<bool> wifi(true);
Toggle("Wi-Fi", wifi);
Toggle(Label("Notifications", {.system_name = "bell"}), notifications);
Toggle("Remember me", remember).toggle_style(CheckboxToggleStyle{});
```

`SwitchToggleStyle` (the default) shows the label then a switch; `CheckboxToggleStyle` shows a
check box then the label.

## TextField and TextArea

A `TextField` is a single-line editor bound to a `std::string`:

```cpp
const State<std::string> name;
TextField("Name", name);
TextField("Search", query, {.on_submit = [=] { search(query()); }});
TextField("Password", password, {.secure = true});
```

Text fields support typing, IME composition (Chinese, Japanese, Korean input), selection with the
pointer and Shift+arrows, word selection by double-click, select-all by triple-click, word
movement with Ctrl+arrows, the clipboard, and undo and redo.

`.secure` shows a bullet per character, and the text can't be copied out. It's a `Prop<bool>`, so
a button can reveal the password:

```cpp
const State<bool> reveal(false);
HStack(TextField("Password", password, {.secure = [=] { return !reveal(); }}),
       Button(Image("eye"), [=] { reveal = !reveal(); }));
```

With `.line_limit` above 1, a field wraps and grows up to that many lines, then scrolls, like a
chat box. Enter submits and Shift+Enter starts a new line. A secure field is always one line.

A `TextArea` is for longer text: notes, messages, documents. Enter starts a new line and
Ctrl+Enter (Cmd+Enter on macOS) runs `on_submit`. It takes the height it's offered and scrolls
through longer text; inside a `ScrollView` it grows with its text instead:

```cpp
TextArea("Notes", note).frame({.height = 200});
ScrollView(TextArea("Start writing...", body));
```

Style text fields with `.text_field_style(...)`: `RoundedBorderTextFieldStyle` (the default) or
`PlainTextFieldStyle`.

To move focus into a field, use `.focused(...)` with a `Binding<bool>`:

```cpp
const State<bool> editing(false);
TextField("Title", title).focused(editing);
Button("Rename", [=] { editing = true; });
```

## Slider

A value in a range, picked by dragging or with the arrow keys, Page Up/Down and Home/End:

```cpp
const State<float> volume(0.5f);
Slider(volume);
Slider(size, {.min = 8, .max = 72, .step = 1});
Slider(brightness, {.on_editing_changed = [=](bool dragging) { if (!dragging) save(); }});
```

A slider takes the width it's offered.

## Stepper

A label with − and + buttons that change an integer:

```cpp
const State<int> guests(2);
Stepper(Text([=] { return std::format("Guests: {}", guests()); }), guests, {.min = 1, .max = 12});
Stepper("Copies", copies);
```

## Picker

One choice among several. The options are pairs of a value and a label:

```cpp
enum class Size { small, medium, large };
const State<Size> size(Size::medium);

Picker(size, {{Size::small, "S"}, {Size::medium, "M"}, {Size::large, "L"}});               // segments
Picker(size, {{Size::small, "Small"}, {Size::medium, "Medium"}, {Size::large, "Large"}}, PickerStyle::menu);
```

The value type needs `==`. `PickerStyle::segmented` shows every option; `PickerStyle::menu` shows
the chosen one on a button that opens a menu.

## ProgressView

```cpp
ProgressView(0.4f);                          // a bar, 40% full
ProgressView([=] { return downloaded() / total(); });
ProgressView();                              // indeterminate activity
```

A determinate bar animates to its new value. The indeterminate one draws continuously while it's
shown, so remove it (with `If`) when the work is done.

## Menus

`MenuButton` opens a menu below it. See [Menus and popups](../menus/).
