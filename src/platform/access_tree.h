// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_ACCESS_TREE_H
#define ELG_PLATFORM_ACCESS_TREE_H

// The accessibility tree (design.md §12) as the platform's adapter takes it: plain data that the
// view layer builds from its elements (view/accessibility.h), and the requests assistive
// technology sends back.

#include <elg/geometry.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace elg::detail {

// What an element is to assistive technology: screen readers see a tree of these. An element
// with no role is transparent: its children join its parent's.
enum class AccessRole {
    none,
    window,
    group,
    label,
    heading,
    button,
    link,
    toggle,   // a switch
    checkbox,
    slider,
    stepper,
    progress,
    text_field,
    text_area, // multiline
    password_field,
    image,
    video,
    list,
    list_item,
    list_box,        // a stack with selectable rows (View::selection)
    list_box_option, // one of its rows
    scroll_view,
    tab,
    menu,
    menu_item,
    popup_button, // a button that opens a menu (MenuButton, Picker)
    separator,    // a line between groups; not focusable
};

// A switch's or check box's state.
enum class ToggleState { off, on, mixed };

struct AccessibilityInfo {
    AccessRole role = AccessRole::none;
    std::string label;       // its name
    std::string value;       // what it holds (a text field's text, a slider's value as text)
    std::string hint;        // a longer description
    std::string placeholder;
    bool hidden = false;          // it and everything inside are left out (decorative)
    bool combine = false;         // children's text becomes its label; they aren't exposed themselves
    bool disabled = false;
    std::optional<ToggleState> toggled; // switches and check boxes
    std::optional<bool> expanded;
    bool selected = false;
    bool multiselectable = false; // list boxes
    std::optional<double> numeric; // sliders, steppers, progress
    std::optional<double> minimum;
    std::optional<double> maximum;
    std::optional<double> step;
    // Actions it supports; the element performs them in accessibility_action().
    bool can_click = false;
    bool can_focus = false;
    bool can_adjust = false;     // increment / decrement
    bool can_set_value = false;  // text or numbers

    friend bool operator==(const AccessibilityInfo&, const AccessibilityInfo&) = default;
};

// What assistive technology asks an element to do.
struct AccessibilityAction {
    enum class Kind { click, focus, increment, decrement, set_value, set_numeric, scroll_into_view };
    Kind kind = Kind::click;
    std::string text;
    double number = 0;
};

struct AccessNode {
    std::uint64_t id = 0;
    AccessibilityInfo info;
    Rect bounds; // window points
    std::vector<std::uint64_t> children;

    friend bool operator==(const AccessNode&, const AccessNode&) = default;
};

struct AccessTree {
    static constexpr std::uint64_t kRoot = 1;
    std::vector<AccessNode> nodes; // the root first
    std::uint64_t focus = kRoot;
    // What carries out actions on each node (valid until the next build).
    // What a screen reader can ask of a node; the node's id names its element
    // (element_with_accessibility_id).
    struct Target {
        std::function<void()> action; // a custom default action
        bool disabled = false;        // a disabled control, or a custom action in a disabled view
    };
    std::unordered_map<std::uint64_t, Target> targets;
};

} // namespace elg::detail

#endif // ELG_PLATFORM_ACCESS_TREE_H
