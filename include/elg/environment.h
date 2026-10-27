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

#include <memory>
#include <string_view>
#include <utility>

namespace elg {

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

class EnvironmentValues {
public:
    // The value for `Key`: the nearest one set above, else the key's default. A tracked read.
    template <class Key>
    typename Key::Value get() const {
        for (const Entry* entry = head_.get(); entry; entry = entry->next.get()) {
            if (entry->key == Key::name) {
                return static_cast<const Prop<typename Key::Value>*>(entry->value.get())->get();
            }
        }
        return Key::default_value(*this);
    }

    // A semantic color in this environment's theme and color scheme. A tracked read.
    Color color(ThemeColor token) const { return detail::resolve_theme_color(*this, token); }

    // Sets `Key` for this environment (and everything derived from it).
    template <class Key>
    void set(Prop<typename Key::Value> value) {
        // Environment-dependent values (e.g. a ThemeColor) resolve against the environment they
        // were set in, so a key's value never depends on itself.
        auto resolved = std::make_shared<const Prop<typename Key::Value>>(value.resolved_in(*this));
        head_ = std::make_shared<const Entry>(Entry{Key::name, std::move(resolved), head_});
    }

private:
    // A persistent list: setting a key prepends, so copies made earlier are unaffected and
    // copying is cheap.
    struct Entry {
        std::string_view key;
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

struct FontKey {
    using Value = Font;
    static constexpr std::string_view name = "elg.font";
    static Font default_value(const EnvironmentValues&) { return Font::body; }
};

struct ColorSchemeKey {
    using Value = ColorScheme;
    static constexpr std::string_view name = "elg.color_scheme";
    // Windows set this from the system setting.
    static ColorScheme default_value(const EnvironmentValues&) { return ColorScheme::light; }
};

struct ThemeKey {
    using Value = Theme;
    static constexpr std::string_view name = "elg.theme";
    static Theme default_value(const EnvironmentValues&) { return Theme::standard(); }
};

// The color of text and unfilled shapes.
struct ForegroundKey {
    using Value = Color;
    static constexpr std::string_view name = "elg.foreground";
    static Color default_value(const EnvironmentValues& environment) {
        return detail::resolve_theme_color(environment, ThemeColor::primary);
    }
};

// The accent color of controls.
struct TintKey {
    using Value = Color;
    static constexpr std::string_view name = "elg.tint";
    static Color default_value(const EnvironmentValues& environment) {
        return detail::resolve_theme_color(environment, ThemeColor::accent);
    }
};

// Controls inside a disabled view don't respond to input and look disabled.
struct DisabledKey {
    using Value = bool;
    static constexpr std::string_view name = "elg.disabled";
    static bool default_value(const EnvironmentValues&) { return false; }
};

struct TextFieldStyleKey {
    using Value = std::shared_ptr<const TextFieldStyle>;
    static constexpr std::string_view name = "elg.text_field_style";
    ELG_API static std::shared_ptr<const TextFieldStyle> default_value(const EnvironmentValues&);
};

struct ToggleStyleKey {
    using Value = std::shared_ptr<const ToggleStyle>;
    static constexpr std::string_view name = "elg.toggle_style";
    ELG_API static std::shared_ptr<const ToggleStyle> default_value(const EnvironmentValues&);
};

struct CheckboxStyleKey {
    using Value = std::shared_ptr<const CheckboxStyle>;
    static constexpr std::string_view name = "elg.checkbox_style";
    ELG_API static std::shared_ptr<const CheckboxStyle> default_value(const EnvironmentValues&);
};

struct NumberInputStyleKey {
    using Value = std::shared_ptr<const NumberInputStyle>;
    static constexpr std::string_view name = "elg.number_input_style";
    ELG_API static std::shared_ptr<const NumberInputStyle> default_value(const EnvironmentValues&);
};

struct ProgressStyleKey {
    using Value = std::shared_ptr<const ProgressStyle>;
    static constexpr std::string_view name = "elg.progress_style";
    ELG_API static std::shared_ptr<const ProgressStyle> default_value(const EnvironmentValues&);
};

struct NavigationStackStyleKey {
    using Value = std::shared_ptr<const NavigationStackStyle>;
    static constexpr std::string_view name = "elg.navigation_stack_style";
    ELG_API static std::shared_ptr<const NavigationStackStyle> default_value(const EnvironmentValues&);
};

// The hover delay for everything inside (on_hover handlers without their own, and controls).
struct HoverDelayKey {
    using Value = HoverDelay;
    static constexpr std::string_view name = "elg.hover_delay";
    static HoverDelay default_value(const EnvironmentValues&) { return {}; }
};

struct PopoverStyleKey {
    using Value = std::shared_ptr<const PopoverStyle>;
    static constexpr std::string_view name = "elg.popover_style";
    ELG_API static std::shared_ptr<const PopoverStyle> default_value(const EnvironmentValues&);
};

struct TooltipStyleKey {
    using Value = std::shared_ptr<const TooltipStyle>;
    static constexpr std::string_view name = "elg.tooltip_style";
    ELG_API static std::shared_ptr<const TooltipStyle> default_value(const EnvironmentValues&);
};

struct MenuStyleKey {
    using Value = std::shared_ptr<const MenuStyle>;
    static constexpr std::string_view name = "elg.menu_style";
    ELG_API static std::shared_ptr<const MenuStyle> default_value(const EnvironmentValues&);
};

struct ButtonStyleKey {
    using Value = std::shared_ptr<const ButtonStyle>;
    static constexpr std::string_view name = "elg.button_style";
    ELG_API static std::shared_ptr<const ButtonStyle> default_value(const EnvironmentValues&);
};

namespace detail {
// The environment of the views being built, while a body or style runs; else nullptr.
ELG_API const EnvironmentValues* current_environment() noexcept;
} // namespace detail

// Reads an environment value while views are built: in a component's body (a struct with
// `View body() const`), a ViewModifier's body, a style's make_body, or a window's content
// function. That's the environment where the view is placed. (A plain function that returns a
// View runs before it's placed, so it sees its caller's environment; for values that depend on
// placement, use a ThemeColor or a closure taking `const EnvironmentValues&` instead.)
// The result is a reader: calling it is a tracked read, so it can be passed as a reactive
// argument:
//   const auto tint = Environment<TintKey>();
//   Circle().fill([=] { return tint().opacity(0.5f); })
template <class Key>
class Environment {
public:
    Environment() {
        const EnvironmentValues* environment = detail::current_environment();
        if (!environment) {
            detail::throw_no_environment();
        }
        environment_ = *environment;
    }

    typename Key::Value operator()() const { return environment_.template get<Key>(); }

private:
    EnvironmentValues environment_;
};

} // namespace elg

#endif // ELG_ENVIRONMENT_H
