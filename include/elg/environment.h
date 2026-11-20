// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_ENVIRONMENT_H
#define ELG_ENVIRONMENT_H

// The environment (design.md §8): values inherited down the view tree, set with modifiers like
// .font(), .tint(), .color_scheme(), or .environment<Key>(value), and read by the views below.
//
// Values are typed by keys:
//
//   struct CompactKey {
//       using Value = bool;
//       static constexpr std::string_view name = "myapp.compact"; // stable and unique
//       static bool default_value(const elg::EnvironmentValues&) { return false; }
//   };
//
//   content.environment<CompactKey>(true)                     // set for everything inside
//   const auto compact = Environment<CompactKey>();           // read in a body...
//   .padding([=] { return compact() ? 4.f : 12.f; })          // ...reactively
//
// Values are reactive arguments: set one from a State and only the views reading that key update
// when it changes.

#include <elg/color.h>
#include <elg/events.h>
#include <elg/export.h>
#include <elg/font.h>
#include <elg/prop.h>
#include <elg/theme.h>

#include <cstdint>
#include <memory>
#include <type_traits>
#include <string>
#include <optional>
#include <string_view>
#include <utility>

namespace elg {

namespace detail {
// A key's identifier: a hash (64-bit FNV-1a) of its name. It's the same wherever the key is
// defined, in the app or in any views module, unlike the key's type or the address of its name.
constexpr std::uint64_t key_id(std::string_view name) noexcept {
    std::uint64_t hash = 0xcbf29ce484222325;
    for (const char c : name) {
        hash = (hash ^ static_cast<unsigned char>(c)) * 0x100000001b3;
    }
    return hash;
}

// The types a key's value may have when languages share it by name (1 to 7), or 0 for another type.
template <class T>
constexpr std::uint8_t value_tag() noexcept {
    if constexpr (std::is_same_v<T, bool>) {
        return 1;
    } else if constexpr (std::is_same_v<T, int>) {
        return 2;
    } else if constexpr (std::is_same_v<T, std::int64_t>) {
        return 3;
    } else if constexpr (std::is_same_v<T, float>) {
        return 4;
    } else if constexpr (std::is_same_v<T, double>) {
        return 5;
    } else if constexpr (std::is_same_v<T, std::string>) {
        return 6;
    } else if constexpr (std::is_same_v<T, Color>) {
        return 7;
    } else {
        return 0;
    }
}

// A key's name hashed with the type of its value (value_tag()), so that keys of the same name with
// values of different types don't see each other: one never reads the other's value as its own.
template <class T>
constexpr std::uint64_t typed_key(std::string_view name) noexcept {
    const std::uint64_t key = key_id(name);
    return value_tag<T>() == 0 ? key : key ^ (0x9e3779b97f4a7c15ULL * value_tag<T>());
}
} // namespace detail

/// The types of value that languages share keys of by name (EnvironmentValues::get_named(),
/// LayoutChild::value_named()).
template <class T>
concept SharedKeyValue = std::is_same_v<T, bool> || std::is_same_v<T, int> || std::is_same_v<T, std::int64_t> ||
                         std::is_same_v<T, float> || std::is_same_v<T, double> || std::is_same_v<T, std::string> ||
                         std::is_same_v<T, Color>;

class ButtonStyle;
class ToggleStyle;
class CheckboxStyle;
class NumberInputStyle;
class ProgressStyle;
class NavigationStackStyle;
class TextFieldStyle;
class PopoverStyle;
class TooltipStyle;
class MenuStyle;
class PickerStyle;
class SliderStyle;
class RowStyle;

/// The environment at one place in the view tree: what a closure taking `const EnvironmentValues&`
/// gets, and what Environment<Key>() reads. Copies are cheap, and setting a key on one doesn't
/// change the others.
class EnvironmentValues {
public:
    /// The value for `Key`: the nearest one set above, else the key's default. A tracked read.
    ///
    /// @tparam Key The key: a type with a `Value` type, a `name` and a `default_value()`.
    /// @return The value.
    template <class Key>
    [[nodiscard]] typename Key::Value get() const {
        constexpr std::uint64_t key = detail::typed_key<typename Key::Value>(Key::name);
        for (const Entry* entry = head_.get(); entry; entry = entry->next.get()) {
            if (entry->key == key) {
                return static_cast<const Prop<typename Key::Value>*>(entry->value.get())->get();
            }
        }
        return Key::default_value(*this);
    }

    /// A semantic color in this environment's theme and color scheme. A tracked read.
    ///
    /// @param token The color to look up.
    /// @return The token's color in ThemeKey's theme, for ColorSchemeKey's scheme.
    [[nodiscard]] Color color(ThemeColor token) const { return detail::resolve_theme_color(*this, token); }

    /// Sets `Key` for this environment (and everything derived from it). Copies made earlier keep
    /// their value.
    ///
    /// @tparam Key The key to set.
    /// @param value A plain or reactive value. One that depends on the environment (a ThemeColor,
    ///   a closure taking `const EnvironmentValues&`) is resolved against this environment as it
    ///   was before the call.
    template <class Key>
    void set(const Prop<typename Key::Value>& value) {
        // Environment-dependent values (e.g. a ThemeColor) resolve against the environment they
        // were set in, so a key's value never depends on itself.
        auto resolved = std::make_shared<const Prop<typename Key::Value>>(value.resolved_in(*this));
        head_ = std::make_shared<const Entry>(
            Entry{detail::typed_key<typename Key::Value>(Key::name), std::move(resolved), head_});
    }

    /// The value set for the key named `name` whose value is a `T`, for language bindings, which share
    /// keys by name: a key of that name set here or by a key type with the same `name` and `Value`. Unlike
    /// get(), there's no default. A tracked read.
    ///
    /// @tparam T The value's type: bool, int, std::int64_t, float, double, std::string or Color. A key of
    ///   the same name with a value of another type reads as unset.
    /// @param name The key's name.
    /// @return The value, or std::nullopt when the key isn't set.
    template <SharedKeyValue T>
    [[nodiscard]] std::optional<T> get_named(std::string_view name) const {
        const std::uint64_t key = detail::typed_key<T>(name);
        for (const Entry* entry = head_.get(); entry; entry = entry->next.get()) {
            if (entry->key == key) {
                return static_cast<const Prop<T>*>(entry->value.get())->get(*this);
            }
        }
        return std::nullopt;
    }

    /// Sets the key named `name`, whose value is a `T`, for language bindings: as set() does for a key
    /// type with that `name` and `Value`.
    ///
    /// @tparam T The value's type: bool, int, std::int64_t, float, double, std::string or Color.
    /// @param name The key's name.
    /// @param value A plain or reactive value, resolved as set() resolves it.
    template <SharedKeyValue T>
    void set_named(std::string_view name, const Prop<T>& value) {
        auto resolved = std::make_shared<const Prop<T>>(value.resolved_in(*this));
        head_ = std::make_shared<const Entry>(Entry{detail::typed_key<T>(name), std::move(resolved), head_});
    }

private:
    // A persistent list: setting a key prepends, so copies made earlier are unaffected and
    // copying is cheap. A key is kept as detail::key_id(Key::name), not as a view of the name,
    // which is in the memory of the code that defined the key: maybe a views module.
    struct Entry {
        std::uint64_t key;
        std::shared_ptr<const void> value; // a Prop<Key::Value>
        std::shared_ptr<const Entry> next;
    };
    std::shared_ptr<const Entry> head_;
};

template <class T>
Prop<T> Prop<T>::resolved_in(const EnvironmentValues& environment) const {
    if (value_.index() != 2) {
        return *this;
    }
    return Prop(Tag{}, [function = std::get<2>(value_), environment] { return function(environment); });
}

// --- Built-in keys ---

/// The font of the text inside, set with .font() and .font_weight().
struct FontKey {
    /// What the key holds.
    using Value = Font;
    static constexpr std::string_view name = "elg.font"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return Font::body.
    static Font default_value(const EnvironmentValues&) { return Font::body; }
};

/// Light or dark, for the ThemeColor tokens inside, set with .color_scheme().
struct ColorSchemeKey {
    /// What the key holds.
    using Value = ColorScheme;
    static constexpr std::string_view name = "elg.color_scheme"; ///< Its stable, unique name.
    /// Used where no view above sets the key. Windows set this from the system setting.
    /// @return ColorScheme::light.
    static ColorScheme default_value(const EnvironmentValues&) { return ColorScheme::light; }
};

/// The Theme that ThemeColor and ThemeBlur tokens inside resolve against, set with .theme().
struct ThemeKey {
    /// What the key holds.
    using Value = Theme;
    static constexpr std::string_view name = "elg.theme"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return Theme::standard().
    static Theme default_value(const EnvironmentValues&) { return Theme::standard(); }
};

/// The color of text and unfilled shapes, set with .foreground_style().
struct ForegroundKey {
    /// What the key holds.
    using Value = Color;
    static constexpr std::string_view name = "elg.foreground"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @param environment Where it's read: its theme and color scheme.
    /// @return The theme's ThemeColor::primary.
    static Color default_value(const EnvironmentValues& environment) {
        return detail::resolve_theme_color(environment, ThemeColor::primary);
    }
};

/// The accent color of controls, set with .tint().
struct TintKey {
    /// What the key holds.
    using Value = Color;
    static constexpr std::string_view name = "elg.tint"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @param environment Where it's read: its theme and color scheme.
    /// @return The theme's ThemeColor::accent.
    static Color default_value(const EnvironmentValues& environment) {
        return detail::resolve_theme_color(environment, ThemeColor::accent);
    }
};

/// Whether the view is disabled, set with .disabled(). Controls inside a disabled view don't respond
/// to input and look disabled.
struct DisabledKey {
    /// What the key holds.
    using Value = bool;
    static constexpr std::string_view name = "elg.disabled"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return false.
    static bool default_value(const EnvironmentValues&) { return false; }
};

/// The style of every TextField inside, set with .text_field_style().
struct TextFieldStyleKey {
    /// What the key holds.
    using Value = std::shared_ptr<const TextFieldStyle>;
    static constexpr std::string_view name = "elg.text_field_style"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return The shared RoundedBorderTextFieldStyle.
    ELG_API static std::shared_ptr<const TextFieldStyle> default_value(const EnvironmentValues&);
};

/// The style of every Toggle inside, set with .toggle_style().
struct ToggleStyleKey {
    /// What the key holds.
    using Value = std::shared_ptr<const ToggleStyle>;
    static constexpr std::string_view name = "elg.toggle_style"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return The shared SwitchToggleStyle.
    ELG_API static std::shared_ptr<const ToggleStyle> default_value(const EnvironmentValues&);
};

/// The style of every Checkbox inside, set with .checkbox_style().
struct CheckboxStyleKey {
    /// What the key holds.
    using Value = std::shared_ptr<const CheckboxStyle>;
    static constexpr std::string_view name = "elg.checkbox_style"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return The shared BoxCheckboxStyle.
    ELG_API static std::shared_ptr<const CheckboxStyle> default_value(const EnvironmentValues&);
};

/// The style of every NumberInput inside, set with .number_input_style().
struct NumberInputStyleKey {
    /// What the key holds.
    using Value = std::shared_ptr<const NumberInputStyle>;
    static constexpr std::string_view name = "elg.number_input_style"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return The shared BoxNumberInputStyle.
    ELG_API static std::shared_ptr<const NumberInputStyle> default_value(const EnvironmentValues&);
};

/// The style of every Progress inside, set with .progress_style().
struct ProgressStyleKey {
    /// What the key holds.
    using Value = std::shared_ptr<const ProgressStyle>;
    static constexpr std::string_view name = "elg.progress_style"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return The shared LinearProgressStyle.
    ELG_API static std::shared_ptr<const ProgressStyle> default_value(const EnvironmentValues&);
};

/// The style of every Picker inside, set with .picker_style().
struct PickerStyleKey {
    /// What the key holds.
    using Value = std::shared_ptr<const PickerStyle>;
    static constexpr std::string_view name = "elg.picker_style"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return The shared SegmentedPickerStyle.
    ELG_API static std::shared_ptr<const PickerStyle> default_value(const EnvironmentValues&);
};

/// The style of every Slider inside, set with .slider_style().
struct SliderStyleKey {
    /// What the key holds.
    using Value = std::shared_ptr<const SliderStyle>;
    static constexpr std::string_view name = "elg.slider_style"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return The shared KnobSliderStyle.
    ELG_API static std::shared_ptr<const SliderStyle> default_value(const EnvironmentValues&);
};

/// The style of every NavigationStack inside, set with .navigation_stack_style().
struct NavigationStackStyleKey {
    /// What the key holds.
    using Value = std::shared_ptr<const NavigationStackStyle>;
    static constexpr std::string_view name = "elg.navigation_stack_style"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return The shared PlainNavigationStackStyle.
    ELG_API static std::shared_ptr<const NavigationStackStyle> default_value(const EnvironmentValues&);
};

/// The hover delay for everything inside (on_hover handlers without their own, and controls), set
/// with .hover_delay().
struct HoverDelayKey {
    /// What the key holds.
    using Value = HoverDelay;
    static constexpr std::string_view name = "elg.hover_delay"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return No delay: `{0, 0}`.
    static HoverDelay default_value(const EnvironmentValues&) { return {}; }
};

/// The style of popovers opened from inside, set with .popover_style().
struct PopoverStyleKey {
    /// What the key holds.
    using Value = std::shared_ptr<const PopoverStyle>;
    static constexpr std::string_view name = "elg.popover_style"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return The shared DefaultPopoverStyle.
    ELG_API static std::shared_ptr<const PopoverStyle> default_value(const EnvironmentValues&);
};

/// The style of tooltips shown from inside, set with .tooltip_style().
struct TooltipStyleKey {
    /// What the key holds.
    using Value = std::shared_ptr<const TooltipStyle>;
    static constexpr std::string_view name = "elg.tooltip_style"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return The shared DefaultTooltipStyle.
    ELG_API static std::shared_ptr<const TooltipStyle> default_value(const EnvironmentValues&);
};

/// The style of menus opened from inside, set with .menu_style().
struct MenuStyleKey {
    /// What the key holds.
    using Value = std::shared_ptr<const MenuStyle>;
    static constexpr std::string_view name = "elg.menu_style"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return The shared DefaultMenuStyle.
    ELG_API static std::shared_ptr<const MenuStyle> default_value(const EnvironmentValues&);
};

/// The style of the selectable rows inside (View::selection()), set with .row_style().
struct RowStyleKey {
    /// What the key holds.
    using Value = std::shared_ptr<const RowStyle>;
    static constexpr std::string_view name = "elg.row_style"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return The shared HighlightRowStyle.
    ELG_API static std::shared_ptr<const RowStyle> default_value(const EnvironmentValues&);
};

/// Inside a selectable row (View::selection): whether it's selected; false elsewhere.
///
/// @code
/// const auto selected = Environment<RowSelectedKey>();
/// Text(mail.sender).foreground_style([=] { return selected() ? ThemeColor::on_accent : ThemeColor::secondary; })
/// @endcode
struct RowSelectedKey {
    /// What the key holds.
    using Value = bool;
    static constexpr std::string_view name = "elg.row_selected"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return false.
    static bool default_value(const EnvironmentValues&) { return false; }
};

/// The style of every Button inside, set with .button_style().
struct ButtonStyleKey {
    /// What the key holds.
    using Value = std::shared_ptr<const ButtonStyle>;
    static constexpr std::string_view name = "elg.button_style"; ///< Its stable, unique name.
    /// Used where no view above sets the key.
    /// @return The shared ProminentButtonStyle.
    ELG_API static std::shared_ptr<const ButtonStyle> default_value(const EnvironmentValues&);
};

namespace detail {
// The environment of the views being built, while a body or style runs; else nullptr.
ELG_API const EnvironmentValues* current_environment() noexcept;
} // namespace detail

/// Reads an environment value while views are built: in a component's body (a struct with
/// `View body() const`), a ViewModifier's body, a style's make_body, or a window's content
/// function. That's the environment where the view is placed. (A plain function that returns a
/// View runs before it's placed, so it sees its caller's environment; for values that depend on
/// placement, use a ThemeColor or a closure taking `const EnvironmentValues&` instead.)
/// The result is a reader: calling it is a tracked read, so it can be passed as a reactive
/// argument:
///
/// @code
/// const auto tint = Environment<TintKey>();
/// Circle().fill([=] { return tint().opacity(0.5f); })
/// @endcode
///
/// @tparam Key The key to read, e.g. TintKey or an app's own.
template <class Key>
class Environment {
public:
    /// Captures the environment of the views being built.
    ///
    /// @throws UsageError When no views are being built.
    Environment() {
        const EnvironmentValues* environment = detail::current_environment();
        if (!environment) {
            detail::throw_no_environment();
        }
        environment_ = *environment;
    }

    /// Reads the value. A tracked read.
    ///
    /// @return The value for `Key` in the captured environment.
    typename Key::Value operator()() const { return environment_.template get<Key>(); }

private:
    EnvironmentValues environment_;
};

} // namespace elg

#endif // ELG_ENVIRONMENT_H
