// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEWS_H
#define ELG_VIEWS_H

// Built-in container and primitive views.

#include <elg/export.h>
#include <elg/graphics_context.h>
#include <elg/layout.h>
#include <elg/prop.h>
#include <elg/view.h>

#include <concepts>
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
// hovered and darker while pressed. `action` runs when it's clicked.
// TODO(M3): ButtonStyle (design.md §8).
ELG_API View Button(View label, std::function<void()> action);
ELG_API View Button(std::string title, std::function<void()> action);

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

    bool (*equals)(const void*, const void*) = [](const void* a, const void* b) {
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
            result.push_back({detail::to_item_key(key(value)), shared, equals,
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

} // namespace elg

#endif // ELG_VIEWS_H
