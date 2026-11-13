// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEWS_H
#define ELG_VIEWS_H

// Built-in container and primitive views.

#include <elg/export.h>
#include <elg/graphics_context.h>
#include <elg/image.h>
#include <elg/layout.h>
#include <elg/prop.h>
#include <elg/reactive.h>
#include <elg/text.h>
#include <elg/view.h>

#include <concepts>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace elg {

namespace detail {
struct ScrollTarget;
struct ScrollTargetAccess;
} // namespace detail

/// How a VStack lines up and spaces its children.
struct VStackOptions {
    HorizontalAlignment alignment = HorizontalAlignment::center; ///< where narrower children go
    OptionalLength spacing;                                      ///< default_stack_spacing when unset
};

/// How an HStack lines up and spaces its children.
struct HStackOptions {
    VerticalAlignment alignment = VerticalAlignment::center; ///< where shorter children go
    OptionalLength spacing;                                  ///< default_stack_spacing when unset
};

/// How a ZStack lines up its children.
struct ZStackOptions {
    Alignment alignment = Alignment::center; ///< where smaller children go
};

/// Children stacked top to bottom.
///
/// @code
/// VStack(a, b, c)
/// VStack({.alignment = HorizontalAlignment::leading, .spacing = 8}, a, b, c)
/// @endcode
///
/// @param options The children's alignment and the spacing between them.
/// @param children The views, top first.
/// @return The stack.
template <std::convertible_to<View>... Children>
View VStack(VStackOptions options, Children&&... children) {
    return VStackLayout(options.alignment, options.spacing)(std::forward<Children>(children)...);
}

/// Like VStack(VStackOptions, Children&&...), with the default options.
///
/// @param children The views, top first.
/// @return The stack.
template <std::convertible_to<View>... Children>
View VStack(Children&&... children) {
    return VStack(VStackOptions{}, std::forward<Children>(children)...);
}

/// Children side by side, leading first.
///
/// @code
/// HStack({.spacing = 4}, Image("star"), Text("Favorites"))
/// @endcode
///
/// @param options The children's alignment and the spacing between them.
/// @param children The views, leading first.
/// @return The stack.
template <std::convertible_to<View>... Children>
View HStack(HStackOptions options, Children&&... children) {
    return HStackLayout(options.alignment, options.spacing)(std::forward<Children>(children)...);
}

/// Like HStack(HStackOptions, Children&&...), with the default options.
///
/// @param children The views, leading first.
/// @return The stack.
template <std::convertible_to<View>... Children>
View HStack(Children&&... children) {
    return HStack(HStackOptions{}, std::forward<Children>(children)...);
}

/// Children drawn on top of each other, first child at the back.
///
/// @param options The children's alignment.
/// @param children The views, back first.
/// @return The stack.
template <std::convertible_to<View>... Children>
View ZStack(ZStackOptions options, Children&&... children) {
    return ZStackLayout(options.alignment)(std::forward<Children>(children)...);
}

/// Like ZStack(ZStackOptions, Children&&...), with the children centered.
///
/// @param children The views, back first.
/// @return The stack.
template <std::convertible_to<View>... Children>
View ZStack(Children&&... children) {
    return ZStack(ZStackOptions{}, std::forward<Children>(children)...);
}

/// Flexible space that expands along its stack's axis. Stacks leave no default spacing next to
/// it.
///
/// @param min_length The shortest it gets; default_stack_spacing when unset.
/// @return The spacer.
ELG_API View Spacer(OptionalLength min_length = {});

/// How a Separator looks.
struct SeparatorOptions {
    Prop<Color> color = ThemeColor::outline; ///< the line's color
    float thickness = 1;                     ///< in points
    float inset = 0;                         ///< left clear at both ends
};

/// A thin line between groups: horizontal (as wide as offered: in a VStack, a menu, a grid row) or
/// vertical (as tall as offered: in an HStack). Assistive technology sees a separator, which isn't
/// focusable; .accessibility_hidden() makes it purely decorative.
///
/// @code
/// VStack(Header(), Separator(), Body())
/// HStack(Bold(), Italic(), Separator(Axis::vertical, {.inset = 4}), Link())
/// @endcode
///
/// @param axis The line's direction.
/// @param options Its color, thickness and inset.
/// @return The separator.
ELG_API View Separator(Axis axis = Axis::horizontal, SeparatorOptions options = {});

/// Nothing: a view that takes no space and draws nothing, e.g. for a branch that shows nothing.
///
/// @return An empty view, the same as View().
ELG_API View EmptyView();

/// Immediate-mode drawing; `draw` runs every time the view is drawn. Takes the proposed size (10
/// points where a dimension is left open). It's drawn again when state that `draw` reads changes;
/// to see what that is, `draw` also runs once when the view is built.
///
/// @param draw Draws the view, in a context that covers its frame.
/// @return The canvas.
ELG_API View Canvas(std::function<void(GraphicsContext&)> draw);

/// A push button: `label` on a rounded background in the environment's tint, lighter while
/// hovered and darker while pressed. Its look comes from the ButtonStyle (.button_style(),
/// elg/styles.h).
///
/// @code
/// Button(Text("Save"), [=] { save(document()); })
/// @endcode
///
/// @param label What the button shows.
/// @param action Runs when the button is clicked, or activated with Space or Enter while it
///   has focus.
/// @return The button.
ELG_API View Button(View label, std::function<void()> action);
/// Like Button(View, std::function<void()>), with a Text label.
///
/// @param title The label's text.
/// @param action Runs when the button is clicked, or activated with Space or Enter while it
///   has focus.
/// @return The button.
ELG_API View Button(Prop<std::string> title, std::function<void()> action);

/// What a TextField does besides editing: submitting, secure entry, more lines, styling, keys.
struct TextFieldOptions {
    std::function<void()> on_submit; ///< Enter
    /// Passwords: a bullet per character, no copying, no input method (IME), and one line; on macOS
    /// Secure Event Input while focused, so other apps can't read the keys. Bind it to
    /// let the user see what they typed:
    ///
    /// @code
    /// TextField("Password", password.binding(), {.secure = [=] { return !reveal(); }})
    /// @endcode
    Prop<bool> secure = false;
    /// More than 1: the field wraps and grows up to this many lines, then scrolls. Enter submits;
    /// Shift+Enter starts a new line. For longer text (notes, documents), use TextArea.
    Prop<int> line_limit = 1;
    /// Styles ranges of the text as it's edited (markdown, mentions shown as chips: TextSpan). Spans
    /// are sorted and don't overlap. Called after every change.
    std::function<std::vector<TextSpan>(std::string_view text)> styler;
    /// Sees key presses before the field does; return true to take one (the field then ignores
    /// it). For keys the field would otherwise use, e.g. Up/Down/Enter in an autocomplete list.
    std::function<bool(const KeyEvent&)> on_key;
};

/// A single-line text editor bound to `text`; its look comes from the TextFieldStyle (a rounded
/// border by default). Typing, IME composition, selection (pointer, Shift+arrows, double-click
/// word, triple-click all), word movement, clipboard, and undo/redo.
///
/// @code
/// TextField("Name", name.binding(), {.on_submit = save})
/// TextField("Password", password.binding(), {.secure = true})
/// @endcode
///
/// @param placeholder Shown in ThemeColor::tertiary while the text is empty.
/// @param text The text: every edit sets it, and the field follows changes made elsewhere.
/// @param options Submitting, secure entry, more lines (`line_limit`), styling and keys.
/// @return The text field.
/// @throws elg::UsageError If `options` has `secure` true and `line_limit` above 1 (when both are
///   plain values).
ELG_API View TextField(Prop<std::string> placeholder, Binding<std::string> text, TextFieldOptions options = {});

/// What a TextArea does besides editing: submitting, styling, keys.
struct TextAreaOptions {
    std::function<void()> on_submit; ///< Ctrl+Enter (Cmd+Enter on macOS); Enter starts a new line
    /// As TextFieldOptions::styler.
    std::function<std::vector<TextSpan>(std::string_view text)> styler;
    std::function<bool(const KeyEvent&)> on_key; ///< as TextFieldOptions::on_key
};

/// A multiline text editor bound to `text`, for notes, messages, and documents. It edits like a
/// TextField, takes the height it's offered, and scrolls through longer text (in a ScrollView, it
/// grows with its text instead). Its look comes from the TextFieldStyle.
///
/// @code
/// TextArea("Notes", note.binding()).frame({.height = 200})
/// @endcode
///
/// @param placeholder Shown in ThemeColor::tertiary while the text is empty.
/// @param text The text: every edit sets it, and the area follows changes made elsewhere.
/// @param options Submitting, styling and keys.
/// @return The text area.
ELG_API View TextArea(Prop<std::string> placeholder, Binding<std::string> text, TextAreaOptions options = {});

/// A Slider's range and step, and what to call around drags.
struct SliderOptions {
    Prop<float> min = 0.f;  ///< the value at the leading end
    Prop<float> max = 1.f;  ///< the value at the trailing end; if not above `min`, `min` + 1
    Prop<float> step = 0.f; ///< 0: continuous
    /// Called with true when a drag starts and false when it ends (e.g. to commit an expensive
    /// change only once).
    std::function<void(bool)> on_editing_changed;
};

/// A value in [min, max] picked by dragging a knob along a track (or with arrows, Page Up/Down,
/// Home/End while focused). Takes the width it's offered. Its look comes from the SliderStyle
/// (.slider_style(), elg/styles.h).
///
/// @code
/// Slider(volume.binding(), {.min = 0, .max = 100, .step = 5})
/// @endcode
///
/// @param value The value; the slider sets it within the range, on a step (counted from `min`).
/// @param options The range, the step, and what to call around drags.
/// @return The slider.
ELG_API View Slider(Binding<float> value, SliderOptions options = {});

/// A NumberInput's range, steps, text, and behavior.
struct NumberInputOptions {
    Prop<double> min = -std::numeric_limits<double>::infinity(); ///< the smallest value; none by default
    Prop<double> max = std::numeric_limits<double>::infinity();  ///< the largest value; none by default
    Prop<double> step = 1.0; ///< the arrows, Up/Down (Shift: 10 steps), and each few points scrubbed
    /// Digits after the point; -1: as many as the value needs (up to 6). Integers show none.
    Prop<int> decimals = -1;
    /// Text for a value, and a value for text (nullopt: not a number), e.g. for units:
    ///
    /// @code
    /// .format = [](double v) { return std::format("{:g} px", v); }
    /// @endcode
    ///
    /// By default, plain numbers; parsing ignores spaces around them.
    std::function<std::string(double)> format;
    std::function<std::optional<double>(std::string_view)> parse; ///< see `format`
    /// Apply each valid number as it's typed. Otherwise typing applies on Enter or when the field
    /// loses focus, so "1." isn't rewritten to "1" under the caret.
    Prop<bool> live = false;
    Prop<bool> shows_buttons = true; ///< the style's increment and decrement buttons
    /// Dragging sideways on the field (while it isn't being edited) changes the value; a click
    /// without dragging edits it.
    Prop<bool> scrubs = true;
};

namespace detail {
ELG_API View make_number_input(Binding<double> value, bool integer, NumberInputOptions options);
} // namespace detail

/// A number you can type, step with the arrows or Up/Down, or scrub by dragging. Text that isn't a
/// number reverts when you leave the field; values stay within [min, max]. Its look comes from the
/// NumberInputStyle (a box with stacked arrows by default). Put a Text beside it for a label.
///
/// @code
/// NumberInput(guests.binding(), {.min = 1, .max = 20})
/// NumberInput(opacity.binding(), {.min = 0, .max = 1, .step = 0.05, .decimals = 2})
/// @endcode
///
/// @param value The number: an int, float or double. An int is set to whole numbers only.
/// @param options The range, steps, text, and behavior.
/// @return The number input.
template <class T>
    requires std::same_as<T, int> || std::same_as<T, float> || std::same_as<T, double>
View NumberInput(const Binding<T>& value, NumberInputOptions options = {}) {
    Binding<double> as_double([value] { return static_cast<double>(value()); },
                              [value](double v) {
                                  if constexpr (std::same_as<T, int>) {
                                      v = std::round(v);
                                  }
                                  value.set(static_cast<T>(v));
                              });
    return detail::make_number_input(std::move(as_double), std::same_as<T, int>, std::move(options));
}
/// Like NumberInput(const Binding<T>&, NumberInputOptions), for a State: a State converts to the
/// Binding it holds.
///
/// @param value The number's state: an int, float or double.
/// @param options The range, steps, text, and behavior.
/// @return The number input.
template <class T>
    requires std::same_as<T, int> || std::same_as<T, float> || std::same_as<T, double>
View NumberInput(const State<T>& value, NumberInputOptions options = {}) {
    return NumberInput(value.binding(), std::move(options));
}

/// How far along something is: a fraction (0...1), or, while it's empty, activity of unknown
/// length. Drawn by a ProgressStyle (a bar by default; CircularProgressStyle for a ring); the
/// fraction animates with the update that changed it.
///
/// @code
/// Progress(0.4f)
/// Progress([=] { return download.fraction(); }) // a std::optional<float>: empty until it's known
/// Progress()                                    // indeterminate
/// @endcode
///
/// Name it with .accessibility_label, or put a Text beside it.
///
/// @param fraction How far along, 0...1; empty (the default): indeterminate.
/// @return The progress indicator.
ELG_API View Progress(Prop<std::optional<float>> fraction = std::nullopt);

namespace detail {
ELG_API View make_picker(std::vector<std::string> labels, std::function<std::size_t()> selected,
                         std::function<void(std::size_t)> select);
} // namespace detail

/// One of several options; `selection` is the chosen option's value. Its look comes from the
/// PickerStyle: segments by default, or MenuPickerStyle for a button that opens a menu of them.
///
/// @code
/// Picker(size.binding(), {{Size::small, "S"}, {Size::medium, "M"}, {Size::large, "L"}})
/// @endcode
///
/// @param selection The chosen option's value; choosing an option sets it. While it matches no
///   option, none is chosen.
/// @param options Each option's value and label, in order.
/// @return The picker.
template <class T>
    requires std::equality_comparable<T>
View Picker(const Binding<T>& selection, std::vector<std::pair<T, std::string>> options) {
    const auto shared = std::make_shared<const std::vector<std::pair<T, std::string>>>(std::move(options));
    std::vector<std::string> labels;
    labels.reserve(shared->size());
    for (const auto& option : *shared) {
        labels.push_back(option.second);
    }
    return detail::make_picker(
        std::move(labels),
        [selection, shared]() -> std::size_t {
            const T current = selection();
            for (std::size_t i = 0; i < shared->size(); ++i) {
                if ((*shared)[i].first == current) {
                    return i;
                }
            }
            return shared->size(); // none
        },
        [selection, shared](std::size_t index) { selection.set((*shared)[index].first); });
}

/// An on/off control bound to `is_on`; its look comes from the ToggleStyle (a switch by default).
/// Click, Space, or Enter flips it.
///
/// @param label What the toggle is for, beside the switch.
/// @param is_on Whether it's on; flipping it sets it.
/// @return The toggle.
ELG_API View Toggle(View label, Binding<bool> is_on);
/// Like Toggle(View, Binding<bool>), with a Text label.
///
/// @param title The label's text.
/// @param is_on Whether it's on; flipping it sets it.
/// @return The toggle.
ELG_API View Toggle(Prop<std::string> title, Binding<bool> is_on);
/// Just the switch, for where a label is beside it already (a settings row). Give it an
/// .accessibility_label(...) so screen readers can name it.
///
/// @param is_on Whether it's on; flipping it sets it.
/// @return The toggle.
ELG_API View Toggle(Binding<bool> is_on);

/// A Checkbox's options.
struct CheckboxOptions {
    /// Some of what it stands for is checked, e.g. "Select all" over a partly selected list:
    ///
    /// @code
    /// Checkbox("Select all", all, {.mixed = [=] { return some() && !every(); }})
    /// @endcode
    ///
    /// Shown with a dash; checking it then checks it fully. Read by screen readers as mixed.
    Prop<bool> mixed = false;
};

/// A check box bound to `is_checked`, for choosing something (as part of a form or a list), where
/// a Toggle switches something on. Its look comes from the CheckboxStyle (a box by default).
/// Click, Space, or Enter checks or unchecks it.
///
/// @param label What the check box is for, beside the box.
/// @param is_checked Whether it's checked; checking or unchecking it sets it.
/// @param options Whether it's mixed.
/// @return The check box.
ELG_API View Checkbox(View label, Binding<bool> is_checked, CheckboxOptions options = {});
/// Like Checkbox(View, Binding<bool>, CheckboxOptions), with a Text label.
///
/// @param title The label's text.
/// @param is_checked Whether it's checked; checking or unchecking it sets it.
/// @param options Whether it's mixed.
/// @return The check box.
ELG_API View Checkbox(Prop<std::string> title, Binding<bool> is_checked, CheckboxOptions options = {});
/// Just the box, for where a label is beside it already (a table row). Give it an
/// .accessibility_label(...) so screen readers can name it.
///
/// @param is_checked Whether it's checked; checking or unchecking it sets it.
/// @param options Whether it's mixed.
/// @return The check box.
ELG_API View Checkbox(Binding<bool> is_checked, CheckboxOptions options = {});

// --- Structure: views that appear, disappear, and repeat with state (design.md §6) ---

/// Shows `then_view` while `condition` holds, otherwise `else_view` (nothing by default):
///
/// @code
/// If([=] { return items().empty(); }, Text("Nothing here"))
/// @endcode
///
/// Switching branches removes the old branch's views, including any state created inside them,
/// and builds the other branch. Inside a stack, the shown branch is a direct child of the stack;
/// an empty branch takes no space.
///
/// @param condition Which branch shows: a plain value, State, or closure.
/// @param then_view Shown while `condition` is true.
/// @param else_view Shown while it's false.
/// @return The view that switches between them.
ELG_API View If(Prop<bool> condition, View then_view, View else_view = View());

namespace detail {

// One ForEach item, type-erased for the library.
struct ForEachItem {
    ItemKey key;
    std::shared_ptr<const void> value;         // the item, to detect changes
    bool (*equals)(const void*, const void*);  // compares two values of this item's type
    std::function<View()> view;                // builds the item's view
};

ELG_API View make_for_each(std::function<std::vector<ForEachItem>()> items);

template <class Source>
struct ItemsOf {
    using type = std::decay_t<Source>;
};

template <class Source>
    requires std::invocable<const Source&>
struct ItemsOf<Source> {
    using type = std::decay_t<std::invoke_result_t<const Source&>>;
};

} // namespace detail

/// Which ways a ScrollView scrolls.
enum class ScrollAxes { vertical = 1, horizontal = 2, both = 3 };

/// Scrolls a ScrollView from code: create one, pass it in the options, and call it from event
/// handlers. Copies refer to the same scroll view.
///
/// @code
/// const auto position = ScrollPosition();
/// ScrollView({.position = position}, LazyVStack(ForEach(messages, &Message::id, Row)))
/// Button("Newest", [=] { position.scroll_to_end(); })
/// @endcode
class ELG_API ScrollPosition {
public:
    /// A position for one scroll view (ScrollViewOptions::position).
    ScrollPosition();

    /// Scrolls to the top, keeping the horizontal offset. Applied in the scroll view's next
    /// layout; before the scroll view is built, in its first.
    ///
    /// @param animated Whether it glides there rather than jumping.
    void scroll_to_top(bool animated = true) const;
    /// Scrolls to the bottom, keeping the horizontal offset. Applied in the scroll view's next
    /// layout; before the scroll view is built, in its first.
    ///
    /// @param animated Whether it glides there rather than jumping.
    void scroll_to_end(bool animated = true) const;
    /// Scrolls the item with this ForEach key (in a lazy stack inside) to the top, without
    /// animating. Nothing happens if no lazy stack inside has the key.
    ///
    /// @param id The item's key, as the ForEach's key function returns it.
    template <class K>
    void scroll_to(const K& id) const {
        scroll_to_key(detail::to_item_key(id));
    }

private:
    void scroll_to_key(ItemKey key) const;

    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::ScrollTarget> target_;
    ELG_PRIVATE_MEMBERS_END
    friend struct detail::ScrollTargetAccess;
};

/// How a ScrollView scrolls.
struct ScrollViewOptions {
    ScrollAxes axes = ScrollAxes::vertical; ///< which ways it scrolls
    Prop<bool> shows_indicators = true;     ///< scroll bars, shown while scrolling or hovered
    std::optional<ScrollPosition> position; ///< for scrolling it from code
    /// Chat logs: starts at the end, and while scrolled to the end it stays there as the content
    /// grows (new messages, images loading), without animating. Scrolled back, it stays put.
    Prop<bool> sticks_to_end = false;
};

/// Shows `content` at its ideal size along the scroll axes, clipped to the space the scroll view
/// takes (all it's offered). Wheel, trackpad, scroll-bar drags, and keys (arrows, Page Up/Down,
/// Home/End, unhandled by the focused view) scroll it; Tab scrolls focused views into sight.
/// Trackpad gestures (macOS) follow the axis they start along, stretch past the ends, and coast.
///
/// @code
/// ScrollView(VStack(rows...))
/// @endcode
///
/// @param content What scrolls.
/// @return The scroll view, scrolling vertically.
ELG_API View ScrollView(View content);
/// Like ScrollView(View), with options: the axes, the scroll bars, a ScrollPosition, sticking to
/// the end.
///
/// @param options How it scrolls.
/// @param content What scrolls.
/// @return The scroll view.
ELG_API View ScrollView(const ScrollViewOptions& options, View content);


/// One view per item of a collection, identified by key:
///
/// @code
/// ForEach(todos, [](const Todo& t) { return t.id; }, [=](const Todo& t) { return TodoRow(t); })
/// @endcode
///
/// When `items` changes, items whose key and value are unchanged keep their views (and state);
/// changed items are rebuilt, removed ones are removed. Items without == count as unchanged.
/// Inside a stack, the items are direct children of the stack; elsewhere they stack vertically.
///
/// @param items A std::vector, or a State / Computed / closure producing one.
/// @param key Returns an item's key: an integer, enum, or string. A member pointer
///   (`&Todo::id`) works too.
/// @param content Builds an item's view.
/// @return The items' views.
template <class Source, class KeyFunction, class ContentFunction>
    requires requires { typename detail::ItemsOf<Source>::type::value_type; }
View ForEach(Source items, KeyFunction key, ContentFunction content) {
    using Items = typename detail::ItemsOf<Source>::type;
    using T = typename Items::value_type;
    static_assert(std::is_invocable_v<const KeyFunction&, const T&>, "the key function takes an item");
    static_assert(std::is_convertible_v<std::invoke_result_t<const ContentFunction&, const T&>, View>,
                  "the content function takes an item and returns a view");

    bool (*equals)(const void*, const void*) = []([[maybe_unused]] const void* a, [[maybe_unused]] const void* b) {
        if constexpr (std::equality_comparable<T>) {
            return *static_cast<const T*>(a) == *static_cast<const T*>(b);
        } else {
            return true; // can't tell: keep the existing view
        }
    };
    Prop<Items> source(std::move(items));
    return detail::make_for_each([source = std::move(source), key = std::move(key), content = std::move(content),
                                  equals] {
        std::vector<detail::ForEachItem> result;
        const Items values = source.get(); // a tracked read: the ForEach updates when it changes
        result.reserve(values.size());
        for (const T& value : values) {
            auto shared = std::make_shared<const T>(value);
            result.push_back({detail::to_item_key(std::invoke(key, value)), shared, equals,
                              [shared, content] { return View(content(*shared)); }});
        }
        return result;
    });
}

/// ForEach over integers or strings, keyed by the values themselves.
///
/// @param items A std::vector of integers or strings, or a State / Computed / closure producing
///   one.
/// @param content Builds an item's view.
/// @return The items' views.
template <class Source, class ContentFunction>
    requires requires { typename detail::ItemsOf<Source>::type::value_type; }
View ForEach(Source items, ContentFunction content) {
    using T = typename detail::ItemsOf<Source>::type::value_type;
    return ForEach(std::move(items), [](const T& value) { return value; }, std::move(content));
}

// --- Menus and popups (design.md §13.1) ---

/// A MenuItem's extras: a shortcut hint, the danger color, a check mark, an icon.
struct MenuItemOptions {
    Prop<std::string> shortcut = std::string(); ///< a hint shown on the right, e.g. "Ctrl+R" (it binds no keys)
    Prop<bool> destructive = false;             ///< shown in the danger color
    std::optional<Prop<bool>> checked;          ///< a check mark in front, e.g. for options
    std::optional<ImageSource> image;           ///< an icon in front: {.image = {{.system_name = "copy"}}}
};

/// A menu command. Choosing it closes the menu, then runs `action`. Disable it with .disabled().
///
/// @param title The command's name.
/// @param action Runs once the menus have closed; it may open a dialog or another menu.
/// @param options A shortcut hint, the danger color, a check mark, an icon.
/// @return The menu item.
ELG_API View MenuItem(Prop<std::string> title, std::function<void()> action, MenuItemOptions options = {});

/// A menu item that opens another menu beside it (on hover, click, or Right).
///
/// @param title The item's name.
/// @param menu Builds the submenu (a Menu) each time it opens.
/// @return The menu item.
ELG_API View Submenu(Prop<std::string> title, std::function<View()> menu);

namespace detail {
ELG_API View make_menu(std::vector<View> items);
} // namespace detail

/// The menu panel: items (MenuItem, Submenu, Separator, or any view) stacked in a styled panel.
/// Up/Down move between items, Enter chooses, Esc closes, Left/Right leave and enter submenus.
///
/// @param items The rows, top first.
/// @return The menu, drawn by the MenuStyle of where it opens.
template <std::convertible_to<View>... Items>
View Menu(Items&&... items) {
    return detail::make_menu({View(std::forward<Items>(items))...});
}

/// Where a MenuButton's menu opens, and how.
struct MenuButtonOptions {
    /// Where the menu opens, as for floating content: below the button by default, flipping when
    /// there isn't room. Above, for buttons near the bottom like a message box's:
    ///
    /// @code
    /// {.placement = {.edge = FloatingEdge::top, .spacing = 4}}
    /// @endcode
    FloatingPlacement placement{.spacing = 4};
    Prop<bool> shows_indicator = true; ///< the chevron after the label
    std::optional<PopupMotion> motion; ///< nullopt: the MenuStyle's
};

/// A button that opens the menu `menu` builds, in a popup window. It's drawn by the ButtonStyle.
///
/// @param label What the button shows, before the chevron.
/// @param menu Builds the menu (a Menu) each time it opens.
/// @param options Where the menu opens, the chevron, and the motion.
/// @return The menu button.
ELG_API View MenuButton(View label, std::function<View()> menu, MenuButtonOptions options = {});
/// Like MenuButton(View, std::function<View()>, MenuButtonOptions), with a Text label.
///
/// @param title The label's text.
/// @param menu Builds the menu (a Menu) each time it opens.
/// @param options Where the menu opens, the chevron, and the motion.
/// @return The menu button.
ELG_API View MenuButton(Prop<std::string> title, std::function<View()> menu, MenuButtonOptions options = {});

/// Lines between a lazy stack's items, in the middle of the spacing:
///
/// @code
/// LazyVStack({.spacing = 0, .separators = StackSeparators{.inset = 16}}, ForEach(...))
/// @endcode
struct StackSeparators {
    Prop<Color> color = ThemeColor::outline; ///< the lines' color
    float inset = 0;                         ///< from the leading edge (the top, in a LazyHStack)
    float thickness = 1;                     ///< in points
};

/// How a LazyVStack lines up, spaces, and separates its items.
struct LazyVStackOptions {
    HorizontalAlignment alignment = HorizontalAlignment::center; ///< where narrower items go
    OptionalLength spacing;                                      ///< default_stack_spacing
    std::optional<StackSeparators> separators;                   ///< lines between items; none by default
};

/// How a LazyHStack lines up, spaces, and separates its items.
struct LazyHStackOptions {
    VerticalAlignment alignment = VerticalAlignment::center; ///< where shorter items go
    OptionalLength spacing;                                  ///< default_stack_spacing
    std::optional<StackSeparators> separators;               ///< lines between items; none by default
};

/// Like VStack / HStack over a ForEach, but only the items in or near the visible part of the
/// enclosing ScrollView are built; the rest are estimated from the sizes seen so far. For long
/// lists (thousands of rows). Items scrolled far away are destroyed with their state.
///
/// @code
/// ScrollView(LazyVStack(ForEach(messages, &Message::id, MessageRow)))
/// @endcode
///
/// Outside a ScrollView every item is built.
///
/// @param options The alignment, spacing, and separators.
/// @param for_each The items; must be a ForEach.
/// @return The lazy stack.
/// @throws elg::UsageError If `for_each` isn't a ForEach.
ELG_API View LazyVStack(LazyVStackOptions options, const View& for_each);
/// Like LazyVStack(LazyVStackOptions, const View&), with the default options.
///
/// @param for_each The items; must be a ForEach.
/// @return The lazy stack.
/// @throws elg::UsageError If `for_each` isn't a ForEach.
ELG_API View LazyVStack(const View& for_each);
/// Like LazyVStack(LazyVStackOptions, const View&), with the items side by side, leading first.
///
/// @param options The alignment, spacing, and separators.
/// @param for_each The items; must be a ForEach.
/// @return The lazy stack.
/// @throws elg::UsageError If `for_each` isn't a ForEach.
ELG_API View LazyHStack(LazyHStackOptions options, const View& for_each);
/// Like LazyHStack(LazyHStackOptions, const View&), with the default options.
///
/// @param for_each The items; must be a ForEach.
/// @return The lazy stack.
/// @throws elg::UsageError If `for_each` isn't a ForEach.
ELG_API View LazyHStack(const View& for_each);

} // namespace elg

#endif // ELG_VIEWS_H
