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

## Toggle and Checkbox

A `Toggle` is an on/off switch bound to a `bool`: it turns something on, like Wi-Fi. A
`Checkbox` chooses something, like an option in a form or an item in a list. Click, Space or
Enter flips either. Screen readers announce them as a switch and a check box.

```cpp
const State<bool> wifi(true);
Toggle("Wi-Fi", wifi);
Toggle(Label("Notifications", {.system_name = "bell"}), notifications);
Checkbox("Remember me", remember);
```

Both are as wide as their label and control. For a settings row, with the label at one edge and
the switch at the other, use `SwitchToggleStyle{{.fills_width = true}}`. Where the label is laid
out separately, `Toggle(binding)` is just the switch and `Checkbox(binding)` just the box; give
them an `.accessibility_label(...)`:

```cpp
HStack(VStack(Text("Wi-Fi"), Text("Connected").foreground_style(ThemeColor::secondary)),
       Spacer(),
       Toggle(wifi).accessibility_label("Wi-Fi"));
```

A check box can be mixed: some of what it stands for is checked. It shows a dash, screen readers
read it as mixed, and checking it checks it fully:

```cpp
const Binding<bool> all([=] { return std::ranges::all_of(files(), &File::selected); },
                        [=](bool on) { select_all(on); });
Checkbox("Select all", all, {.mixed = [=] { return some_selected() && !all(); }});
```

Style them with `.toggle_style(...)` and `.checkbox_style(...)`. The built-in
`SwitchToggleStyle` and `BoxCheckboxStyle` take options for their colors, sizes and animation:

```cpp
Toggle("Wi-Fi", wifi).toggle_style(SwitchToggleStyle{{.on = ThemeColor::success, .width = 48, .height = 28}});
Checkbox("Agree", agreed).checkbox_style(BoxCheckboxStyle{{.size = 16, .corner_radius = 3}});
```

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

## NumberInput

A number bound to an `int`, `float` or `double`. You can type it, step it with the arrows or
Up/Down (Shift for 10 steps), or drag sideways across it to scrub it:

```cpp
const State<int> guests(2);
HStack(Text("Guests"), NumberInput(guests, {.min = 1, .max = 12}));
NumberInput(opacity, {.min = 0, .max = 1, .step = 0.05, .decimals = 2});
NumberInput(width, {.format = [](double v) { return std::format("{:g} px", v); },
                    .parse = parse_pixels});
```

What you type applies when you press Enter or leave the field, so a half-typed `1.` isn't
rewritten under the caret; text that isn't a number goes back to the value. With `.live = true`,
each valid number applies as you type. Values always stay within `min` and `max`.

A click edits the number; a press that moves sideways scrubs it instead, by one `step` every few
points. Turn that off with `.scrubs = false`, and the arrows with `.shows_buttons = false`.

`BoxNumberInputStyle` (the default) draws a box with stacked arrows, with options for its colors,
corners and padding; `PlainNumberInputStyle` is just the number, for a box of your own. Screen
readers see one spin button with the value and range.

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
