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
#include <cstddef>
#include <cstdint>
#include <functional>
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

ELG_API View EmptyView();

// Immediate-mode drawing; `draw` runs every time the view is drawn. Takes the proposed size.
ELG_API View Canvas(std::function<void(GraphicsContext&)> draw);

// A push button: `label` on a rounded background in the environment's tint, lighter while
// hovered and darker while pressed. `action` runs when it's clicked (or activated with Space or
// Enter while focused). Its look comes from the ButtonStyle (.button_style(), elg/styles.h).
ELG_API View Button(View label, std::function<void()> action);
ELG_API View Button(std::string title, std::function<void()> action);

struct TextFieldOptions {
    std::function<void()> on_submit; // Enter
    // More than 1: the field wraps and grows up to this many lines, then scrolls. Enter submits;
    // Shift+Enter starts a new line.
    int max_lines = 1;
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
ELG_API View TextField(std::string placeholder, Binding<std::string> text, TextFieldOptions options = {});
// A TextField for passwords: shows a bullet per character, and can't be copied from.
ELG_API View SecureField(std::string placeholder, Binding<std::string> text, TextFieldOptions options = {});

struct SliderOptions {
    float min = 0;
    float max = 1;
    float step = 0; // 0: continuous
    // Called with true when a drag starts and false when it ends (e.g. to commit an expensive
    // change only once).
    std::function<void(bool)> on_editing_changed;
};

// A value in [min, max] picked by dragging a knob along a track (or with arrows, Page Up/Down,
// Home/End while focused). Takes the width it's offered.
//   Slider(volume.binding(), {.min = 0, .max = 100, .step = 5})
ELG_API View Slider(Binding<float> value, SliderOptions options = {});

struct StepperOptions {
    int min = INT_MIN;
    int max = INT_MAX;
    int step = 1;
};

// The label, then - and + buttons that change `value` by `step` within [min, max].
//   Stepper(Text([=] { return std::format("Guests: {}", guests()); }), guests.binding(), {.min = 1})
ELG_API View Stepper(View label, Binding<int> value, StepperOptions options = {});
ELG_API View Stepper(std::string title, Binding<int> value, StepperOptions options = {});

// A bar filled to `fraction` (0...1); animates with the value.
ELG_API View ProgressView(Prop<float> fraction);
// Indeterminate: activity of unknown length. Renders continuously while shown.
ELG_API View ProgressView();

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
ELG_API View Toggle(std::string title, Binding<bool> is_on);

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
    // Scrolls the item with this ForEach key (in a lazy stack or List inside) to the top.
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
    bool shows_indicators = true; // scroll bars, shown while scrolling or hovered
    std::optional<ScrollPosition> position;
    // Chat logs: starts at the end, and while scrolled to the end it stays there as the content
    // grows (new messages, images loading), without animating. Scrolled back, it stays put.
    bool sticks_to_end = false;
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

// A thin line separating groups: horizontal in a VStack (and menus), full width.
ELG_API View Divider();

struct MenuItemOptions {
    std::string shortcut;              // a hint shown on the right, e.g. "Ctrl+R"
    bool destructive = false;          // shown in the danger color
    std::optional<Prop<bool>> checked; // a check mark in front, e.g. for options
    std::optional<ImageSource> image;  // an icon in front: {.image = {{.system_name = "copy"}}}
};

// A menu command. Choosing it closes the menu, then runs `action`. Disable it with .disabled().
ELG_API View MenuItem(std::string title, std::function<void()> action, MenuItemOptions options = {});

// A menu item that opens another menu beside it (on hover, click, or Right).
ELG_API View Submenu(std::string title, std::function<View()> menu);

namespace detail {
ELG_API View make_menu(std::vector<View> items);
} // namespace detail

// The menu panel: items (MenuItem, Submenu, Divider, or any view) stacked in a styled panel.
// Up/Down move between items, Enter chooses, Esc closes, Left/Right leave and enter submenus.
template <std::convertible_to<View>... Items>
View Menu(Items&&... items) {
    return detail::make_menu({View(std::forward<Items>(items))...});
}

// Which side of its button a MenuButton's menu opens on (it flips if there isn't room).
enum class MenuEdge { below, above };

struct MenuButtonOptions {
    MenuEdge opens = MenuEdge::below; // above: for buttons near the bottom, like a message box's
    bool shows_indicator = true;      // the chevron after the label
};

// A button that opens the menu `menu` builds, below it (or as `options` say).
ELG_API View MenuButton(View label, std::function<View()> menu);
ELG_API View MenuButton(View label, std::function<View()> menu, MenuButtonOptions options);
ELG_API View MenuButton(std::string title, std::function<View()> menu);

struct LazyVStackOptions {
    HorizontalAlignment alignment = HorizontalAlignment::center;
    OptionalLength spacing; // default_stack_spacing
};

struct LazyHStackOptions {
    VerticalAlignment alignment = VerticalAlignment::center;
    OptionalLength spacing;
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

// A scrolling list of rows: lazy, with separators, row padding, and a hover highlight, on a
// rounded surface. `for_each` must be a ForEach.
//   List(ForEach(contacts, &Contact::id, [](const Contact& c) { return ContactRow(c); }))
struct ListOptions {
    std::optional<ScrollPosition> position; // to scroll it from code
};

ELG_API View List(View for_each);
ELG_API View List(ListOptions options, View for_each);

} // namespace elg

#endif // ELG_VIEWS_H
