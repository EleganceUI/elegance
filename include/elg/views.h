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

struct VStackOptions {
    HorizontalAlignment alignment = HorizontalAlignment::center;
    OptionalLength spacing; // default_stack_spacing when unset
};

struct HStackOptions {
    VerticalAlignment alignment = VerticalAlignment::center;
    OptionalLength spacing;
};

struct ZStackOptions {
    Alignment alignment = Alignment::center;
};

// VStack(a, b, c) or VStack({.alignment = HorizontalAlignment::leading, .spacing = 8}, a, b, c)
template <std::convertible_to<View>... Children>
View VStack(VStackOptions options, Children&&... children) {
    return VStackLayout(options.alignment, options.spacing)(std::forward<Children>(children)...);
}

template <std::convertible_to<View>... Children>
View VStack(Children&&... children) {
    return VStack(VStackOptions{}, std::forward<Children>(children)...);
}

template <std::convertible_to<View>... Children>
View HStack(HStackOptions options, Children&&... children) {
    return HStackLayout(options.alignment, options.spacing)(std::forward<Children>(children)...);
}

template <std::convertible_to<View>... Children>
View HStack(Children&&... children) {
    return HStack(HStackOptions{}, std::forward<Children>(children)...);
}

// Children drawn on top of each other, first child at the back.
template <std::convertible_to<View>... Children>
View ZStack(ZStackOptions options, Children&&... children) {
    return ZStackLayout(options.alignment)(std::forward<Children>(children)...);
}

template <std::convertible_to<View>... Children>
View ZStack(Children&&... children) {
    return ZStack(ZStackOptions{}, std::forward<Children>(children)...);
}

// Flexible space that expands along its stack's axis.
ELG_API View Spacer(OptionalLength min_length = {});

struct SeparatorOptions {
    Prop<Color> color = ThemeColor::outline;
    float thickness = 1;
    float inset = 0; // left clear at both ends
};

// A thin line between groups: horizontal (as wide as offered: in a VStack, a menu, a grid row) or
// vertical (as tall as offered: in an HStack). Assistive technology sees a separator, which isn't
// focusable; .accessibility_hidden() makes it purely decorative.
//   VStack(Header(), Separator(), Body())
//   HStack(Bold(), Italic(), Separator(Axis::vertical, {.inset = 4}), Link())
ELG_API View Separator(Axis axis = Axis::horizontal, SeparatorOptions options = {});

ELG_API View EmptyView();

// Immediate-mode drawing; `draw` runs every time the view is drawn. Takes the proposed size.
ELG_API View Canvas(std::function<void(GraphicsContext&)> draw);

// A push button: `label` on a rounded background in the environment's tint, lighter while
// hovered and darker while pressed. `action` runs when it's clicked (or activated with Space or
// Enter while focused). Its look comes from the ButtonStyle (.button_style(), elg/styles.h).
ELG_API View Button(View label, std::function<void()> action);
ELG_API View Button(Prop<std::string> title, std::function<void()> action);

struct TextFieldOptions {
    std::function<void()> on_submit; // Enter
    // Passwords: a bullet per character, no copying, no input method, and one line. Bind it to
    // let the user see what they typed:
    //   TextField("Password", password.binding(), {.secure = [=] { return !reveal(); }})
    Prop<bool> secure = false;
    // More than 1: the field wraps and grows up to this many lines, then scrolls. Enter submits;
    // Shift+Enter starts a new line. For longer text (notes, documents), use TextArea.
    Prop<int> line_limit = 1;
    // Styles ranges of the text as it's edited (markdown, mentions shown as chips: TextSpan). Spans
    // are sorted and don't overlap. Called after every change.
    std::function<std::vector<TextSpan>(std::string_view text)> styler;
    // Sees key presses before the field does; return true to take one (the field then ignores
    // it). For keys the field would otherwise use, e.g. Up/Down/Enter in an autocomplete list.
    std::function<bool(const KeyEvent&)> on_key;
};

// A single-line text editor bound to `text`; its look comes from the TextFieldStyle (a rounded
// border by default). Typing, IME composition, selection (pointer, Shift+arrows, double-click
// word, triple-click all), word movement, clipboard, and undo/redo.
//   TextField("Name", name.binding(), {.on_submit = save})
//   TextField("Password", password.binding(), {.secure = true})
ELG_API View TextField(Prop<std::string> placeholder, Binding<std::string> text, TextFieldOptions options = {});

struct TextAreaOptions {
    std::function<void()> on_submit; // Ctrl+Enter (Cmd+Enter on macOS); Enter starts a new line
    // As TextFieldOptions::styler and ::on_key.
    std::function<std::vector<TextSpan>(std::string_view text)> styler;
    std::function<bool(const KeyEvent&)> on_key;
};

// A multiline text editor bound to `text`, for notes, messages, and documents. It edits like a
// TextField, takes the height it's offered, and scrolls through longer text (in a ScrollView, it
// grows with its text instead). Its look comes from the TextFieldStyle.
//   TextArea("Notes", note.binding()).frame({.height = 200})
ELG_API View TextArea(Prop<std::string> placeholder, Binding<std::string> text, TextAreaOptions options = {});

struct SliderOptions {
    Prop<float> min = 0.f;
    Prop<float> max = 1.f;
    Prop<float> step = 0.f; // 0: continuous
    // Called with true when a drag starts and false when it ends (e.g. to commit an expensive
    // change only once).
    std::function<void(bool)> on_editing_changed;
};

// A value in [min, max] picked by dragging a knob along a track (or with arrows, Page Up/Down,
// Home/End while focused). Takes the width it's offered.
//   Slider(volume.binding(), {.min = 0, .max = 100, .step = 5})
ELG_API View Slider(Binding<float> value, SliderOptions options = {});

struct NumberInputOptions {
    Prop<double> min = -std::numeric_limits<double>::infinity();
    Prop<double> max = std::numeric_limits<double>::infinity();
    Prop<double> step = 1.0; // the arrows, Up/Down (Shift: 10 steps), and each few points scrubbed
    // Digits after the point; -1: as many as the value needs (up to 6). Integers show none.
    Prop<int> decimals = -1;
    // Text for a value, and a value for text (nullopt: not a number), e.g. for units:
    //   .format = [](double v) { return std::format("{:g} px", v); }
    // By default, plain numbers; parsing ignores spaces around them.
    std::function<std::string(double)> format;
    std::function<std::optional<double>(std::string_view)> parse;
    // Apply each valid number as it's typed. Otherwise typing applies on Enter or when the field
    // loses focus, so "1." isn't rewritten to "1" under the caret.
    Prop<bool> live = false;
    Prop<bool> shows_buttons = true; // the style's increment and decrement buttons
    // Dragging sideways on the field (while it isn't being edited) changes the value; a click
    // without dragging edits it.
    Prop<bool> scrubs = true;
};

namespace detail {
ELG_API View make_number_input(Binding<double> value, bool integer, NumberInputOptions options);
} // namespace detail

// A number you can type, step with the arrows or Up/Down, or scrub by dragging. Text that isn't a
// number reverts when you leave the field; values stay within [min, max]. Its look comes from the
// NumberInputStyle (a box with stacked arrows by default). Put a Text beside it for a label.
//   NumberInput(guests.binding(), {.min = 1, .max = 20})
//   NumberInput(opacity.binding(), {.min = 0, .max = 1, .step = 0.05, .decimals = 2})
template <class T>
    requires std::same_as<T, int> || std::same_as<T, float> || std::same_as<T, double>
View NumberInput(Binding<T> value, NumberInputOptions options = {}) {
    Binding<double> as_double([value] { return static_cast<double>(value()); },
                              [value](double v) {
                                  if constexpr (std::same_as<T, int>) {
                                      v = std::round(v);
                                  }
                                  value.set(static_cast<T>(v));
                              });
    return detail::make_number_input(std::move(as_double), std::same_as<T, int>, std::move(options));
}
// A State converts to the Binding it holds.
template <class T>
    requires std::same_as<T, int> || std::same_as<T, float> || std::same_as<T, double>
View NumberInput(const State<T>& value, NumberInputOptions options = {}) {
    return NumberInput(value.binding(), std::move(options));
}

// How far along something is: a fraction (0...1), or, while it's empty, activity of unknown
// length. Drawn by a ProgressStyle (a bar by default; CircularProgressStyle for a ring); the
// fraction animates with the update that changed it.
//   Progress(0.4f)
//   Progress([=] { return download.fraction(); }) // a std::optional<float>: empty until it's known
//   Progress()                                    // indeterminate
// Name it with .accessibility_label, or put a Text beside it.
ELG_API View Progress(Prop<std::optional<float>> fraction = std::nullopt);

enum class PickerStyle {
    segmented, // every option visible, as segments
    menu,      // the chosen option on a button; the others in a menu
};

namespace detail {
ELG_API View make_segmented_picker(std::vector<std::string> labels, std::function<std::size_t()> selected,
                                   std::function<void(std::size_t)> select);
ELG_API View make_menu_picker(std::vector<std::string> labels, std::function<std::size_t()> selected,
                              std::function<void(std::size_t)> select);
} // namespace detail

// One of several options; `selection` is the chosen option's value. Shown as segments, or with
// PickerStyle::menu as a button that opens a menu of the options.
//   Picker(size.binding(), {{Size::small, "S"}, {Size::medium, "M"}, {Size::large, "L"}})
template <class T>
    requires std::equality_comparable<T>
View Picker(Binding<T> selection, std::vector<std::pair<T, std::string>> options,
            PickerStyle style = PickerStyle::segmented) {
    const auto shared = std::make_shared<const std::vector<std::pair<T, std::string>>>(std::move(options));
    std::vector<std::string> labels;
    labels.reserve(shared->size());
    for (const auto& option : *shared) {
        labels.push_back(option.second);
    }
    auto make = style == PickerStyle::menu ? &detail::make_menu_picker : &detail::make_segmented_picker;
    return make(
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

// An on/off control bound to `is_on`; its look comes from the ToggleStyle (a switch by default).
// Click, Space, or Enter flips it.
ELG_API View Toggle(View label, Binding<bool> is_on);
ELG_API View Toggle(Prop<std::string> title, Binding<bool> is_on);
// Just the switch, for where a label is beside it already (a settings row). Give it an
// .accessibility_label(...) so screen readers can name it.
ELG_API View Toggle(Binding<bool> is_on);

struct CheckboxOptions {
    // Some of what it stands for is checked, e.g. "Select all" over a partly selected list:
    //   Checkbox("Select all", all, {.mixed = [=] { return some() && !every(); }})
    // Shown with a dash; checking it then checks it fully. Read by screen readers as mixed.
    Prop<bool> mixed = false;
};

// A check box bound to `is_checked`, for choosing something (as part of a form or a list), where
// a Toggle switches something on. Its look comes from the CheckboxStyle (a box by default).
// Click, Space, or Enter checks or unchecks it.
ELG_API View Checkbox(View label, Binding<bool> is_checked, CheckboxOptions options = {});
ELG_API View Checkbox(Prop<std::string> title, Binding<bool> is_checked, CheckboxOptions options = {});
// Just the box, for where a label is beside it already (a table row). Give it an
// .accessibility_label(...) so screen readers can name it.
ELG_API View Checkbox(Binding<bool> is_checked, CheckboxOptions options = {});

// --- Structure: views that appear, disappear, and repeat with state (design.md §6) ---

// Shows `then_view` while `condition` holds, otherwise `else_view` (nothing by default):
//   If([=] { return items().empty(); }, Text("Nothing here"))
// Switching branches removes the old branch's views, including any state created inside them,
// and builds the other branch. Inside a stack, the shown branch is a direct child of the stack;
// an empty branch takes no space.
ELG_API View If(Prop<bool> condition, View then_view, View else_view = View());

// Identifies a ForEach item across updates.
using ItemKey = std::variant<std::int64_t, std::string>;

namespace detail {

// One ForEach item, type-erased for the library.
struct ForEachItem {
    ItemKey key;
    std::shared_ptr<const void> value;         // the item, to detect changes
    bool (*equals)(const void*, const void*);  // compares two values of this item's type
    std::function<View()> view;                // builds the item's view
};

ELG_API View make_for_each(std::function<std::vector<ForEachItem>()> items);

template <class K>
ItemKey to_item_key(const K& key) {
    if constexpr (std::is_integral_v<K> || std::is_enum_v<K>) {
        return static_cast<std::int64_t>(key);
    } else {
        static_assert(std::is_convertible_v<const K&, std::string_view>,
                      "ForEach keys must be integers, enums, or strings");
        return std::string(std::string_view(key));
    }
}

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

enum class ScrollAxes { vertical = 1, horizontal = 2, both = 3 };

// Scrolls a ScrollView from code: create one, pass it in the options, and call it from event
// handlers. Copies refer to the same scroll view.
//   const auto position = ScrollPosition();
//   ScrollView({.position = position}, LazyVStack(ForEach(messages, &Message::id, Row)))
//   Button("Newest", [=] { position.scroll_to_end(); })
class ELG_API ScrollPosition {
public:
    ScrollPosition();

    void scroll_to_top(bool animated = true) const;
    void scroll_to_end(bool animated = true) const;
    // Scrolls the item with this ForEach key (in a lazy stack inside) to the top.
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

struct ScrollViewOptions {
    ScrollAxes axes = ScrollAxes::vertical;
    Prop<bool> shows_indicators = true; // scroll bars, shown while scrolling or hovered
    std::optional<ScrollPosition> position;
    // Chat logs: starts at the end, and while scrolled to the end it stays there as the content
    // grows (new messages, images loading), without animating. Scrolled back, it stays put.
    Prop<bool> sticks_to_end = false;
};

// Shows `content` at its ideal size along the scroll axes, clipped to the space the scroll view
// takes (all it's offered). Wheel, trackpad, scroll-bar drags, and keys (arrows, Page Up/Down,
// Home/End, unhandled by the focused view) scroll it; Tab scrolls focused views into sight.
// Trackpad gestures (macOS) follow the axis they start along, stretch past the ends, and coast.
//   ScrollView(VStack(rows...))
ELG_API View ScrollView(View content);
ELG_API View ScrollView(ScrollViewOptions options, View content);


// One view per item of a collection, identified by key:
//   ForEach(todos, [](const Todo& t) { return t.id; }, [=](const Todo& t) { return TodoRow(t); })
// `items` is a std::vector, or a State / Computed / closure producing one. When it changes, items
// whose key and value are unchanged keep their views (and state); changed items are rebuilt,
// removed ones are removed. Keys are integers, enums, or strings. Inside a stack, the items are
// direct children of the stack; elsewhere they stack vertically.
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

// ForEach over integers or strings, keyed by the values themselves.
template <class Source, class ContentFunction>
    requires requires { typename detail::ItemsOf<Source>::type::value_type; }
View ForEach(Source items, ContentFunction content) {
    using T = typename detail::ItemsOf<Source>::type::value_type;
    return ForEach(std::move(items), [](const T& value) -> const T& { return value; }, std::move(content));
}

// --- Menus and popups (design.md §13.1) ---

struct MenuItemOptions {
    Prop<std::string> shortcut = std::string(); // a hint shown on the right, e.g. "Ctrl+R"
    Prop<bool> destructive = false;             // shown in the danger color
    std::optional<Prop<bool>> checked;          // a check mark in front, e.g. for options
    std::optional<ImageSource> image;           // an icon in front: {.image = {{.system_name = "copy"}}}
};

// A menu command. Choosing it closes the menu, then runs `action`. Disable it with .disabled().
ELG_API View MenuItem(Prop<std::string> title, std::function<void()> action, MenuItemOptions options = {});

// A menu item that opens another menu beside it (on hover, click, or Right).
ELG_API View Submenu(Prop<std::string> title, std::function<View()> menu);

namespace detail {
ELG_API View make_menu(std::vector<View> items);
} // namespace detail

// The menu panel: items (MenuItem, Submenu, Separator, or any view) stacked in a styled panel.
// Up/Down move between items, Enter chooses, Esc closes, Left/Right leave and enter submenus.
template <std::convertible_to<View>... Items>
View Menu(Items&&... items) {
    return detail::make_menu({View(std::forward<Items>(items))...});
}

struct MenuButtonOptions {
    // Where the menu opens, as for floating content: below the button by default, flipping when
    // there isn't room. Above, for buttons near the bottom like a message box's:
    //   {.placement = {.edge = FloatingEdge::top, .spacing = 4}}
    FloatingPlacement placement{.spacing = 4};
    Prop<bool> shows_indicator = true; // the chevron after the label
    std::optional<PopupMotion> motion; // nullopt: the MenuStyle's
};

// A button that opens the menu `menu` builds, in a popup window.
ELG_API View MenuButton(View label, std::function<View()> menu, MenuButtonOptions options = {});
ELG_API View MenuButton(Prop<std::string> title, std::function<View()> menu, MenuButtonOptions options = {});

// Lines between a lazy stack's items, in the middle of the spacing:
//   LazyVStack({.spacing = 0, .separators = StackSeparators{.inset = 16}}, ForEach(...))
struct StackSeparators {
    Prop<Color> color = ThemeColor::outline;
    float inset = 0; // from the leading edge (the top, in a LazyHStack)
    float thickness = 1;
};

struct LazyVStackOptions {
    HorizontalAlignment alignment = HorizontalAlignment::center;
    OptionalLength spacing; // default_stack_spacing
    std::optional<StackSeparators> separators;
};

struct LazyHStackOptions {
    VerticalAlignment alignment = VerticalAlignment::center;
    OptionalLength spacing;
    std::optional<StackSeparators> separators;
};

// Like VStack / HStack over a ForEach, but only the items in or near the visible part of the
// enclosing ScrollView are built; the rest are estimated from the sizes seen so far. For long
// lists (thousands of rows). Items scrolled far away are destroyed with their state.
//   ScrollView(LazyVStack(ForEach(messages, &Message::id, MessageRow)))
// `for_each` must be a ForEach. Outside a ScrollView every item is built.
ELG_API View LazyVStack(LazyVStackOptions options, View for_each);
ELG_API View LazyVStack(View for_each);
ELG_API View LazyHStack(LazyHStackOptions options, View for_each);
ELG_API View LazyHStack(View for_each);

} // namespace elg

#endif // ELG_VIEWS_H
