// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Styles: the built-in ones, styles implemented in Rust, their configurations, and the modifiers
// that put them in the environment.
//
// A style crosses as `std::shared_ptr<const XStyle>*`, as the environment holds it. A Rust style's
// make_body gets a heap copy of the configuration (args, owned by the Rust side) and writes the
// body into ret (View*). Configuration accessors take `which`, an index into the configuration's
// views, states (Signal<bool>, tracked reads) or actions, in the order the header comments list.

#include "shim.h"

using namespace elg;
using namespace elg_shim;

namespace {

// Calls a Rust part builder with a copy of the configuration.
template <class Config>
View call(const RustFn& f, const Config& config) {
    View result;
    f.call(new Config(config), &result);
    return result;
}

// make_body only.
template <class Style, class Config>
class RustStyle final : public Style {
public:
    explicit RustStyle(SharedFn f) : f_(std::move(f)) {}
    [[nodiscard]] View make_body(const Config& config) const override { return call(*f_, config); }

private:
    SharedFn f_;
};

// make_body and animation() (toggles, check boxes).
template <class Style, class Config>
class RustAnimatedStyle final : public Style {
public:
    RustAnimatedStyle(SharedFn f, std::optional<Animation> animation) : f_(std::move(f)), animation_(std::move(animation)) {}
    [[nodiscard]] View make_body(const Config& config) const override { return call(*f_, config); }
    [[nodiscard]] std::optional<Animation> animation() const override { return animation_; }

private:
    SharedFn f_;
    std::optional<Animation> animation_;
};

// make_body and motion() (popovers, tooltips).
template <class Style, class Config>
class RustPopupStyle final : public Style {
public:
    RustPopupStyle(SharedFn f, std::optional<PopupMotion> motion) : f_(std::move(f)), motion_(std::move(motion)) {}
    [[nodiscard]] View make_body(const Config& config) const override { return call(*f_, config); }
    [[nodiscard]] PopupMotion motion() const override { return motion_ ? *motion_ : Style::motion(); }

private:
    SharedFn f_;
    std::optional<PopupMotion> motion_;
};

class RustSliderStyle final : public SliderStyle {
public:
    RustSliderStyle(SharedFn track, SharedFn fill, SharedFn thumb)
        : track_(std::move(track)), fill_(std::move(fill)), thumb_(std::move(thumb)) {}
    [[nodiscard]] View make_track(const SliderConfiguration& slider) const override { return call(*track_, slider); }
    [[nodiscard]] View make_fill(const SliderConfiguration& slider) const override {
        return fill_ ? call(*fill_, slider) : SliderStyle::make_fill(slider);
    }
    [[nodiscard]] View make_thumb(const SliderConfiguration& slider) const override { return call(*thumb_, slider); }

private:
    SharedFn track_, fill_, thumb_;
};

class RustMenuStyle final : public MenuStyle {
public:
    RustMenuStyle(SharedFn body, SharedFn item, std::optional<PopupMotion> motion)
        : body_(std::move(body)), item_(std::move(item)), motion_(std::move(motion)) {}
    [[nodiscard]] View make_body(const MenuConfiguration& menu) const override { return call(*body_, menu); }
    [[nodiscard]] View make_item(const MenuItemConfiguration& item) const override {
        return item_ ? call(*item_, item) : MenuStyle::make_item(item);
    }
    [[nodiscard]] PopupMotion motion() const override { return motion_ ? *motion_ : MenuStyle::motion(); }

private:
    SharedFn body_, item_;
    std::optional<PopupMotion> motion_;
};

template <class Style, class Concrete, class... Args>
std::shared_ptr<const Style>* make_style(Args&&... args) {
    return new std::shared_ptr<const Style>(std::make_shared<const Concrete>(std::forward<Args>(args)...));
}

// A Color prop given by a nullable pointer: `given` is the pointer, `p` the caller's copy of what it
// points to (named in Consumed), which this takes over.
std::optional<Prop<Color>> optional_color(const ElgProp* given, ElgProp& p) {
    return given ? std::optional<Prop<Color>>(prop<Color>(p)) : std::nullopt;
}

} // namespace

// Per style family: clone and free the handle, set it for a subtree, and copy a configuration.
// A doc comment can't go inside a macro, so the C API functions these macros define are documented
// here. `name` is the style family (button, toggle, ...), `Style` its style class and `Config` its
// configuration.
//
// STYLE_HANDLE(name, Style) defines:
//   elg_<name>_style_clone(s): copies a style handle (`s`, borrowed); both point at the same style.
//     Returns the copy, owned by the caller, or null on failure.
//   elg_<name>_style_free(s): frees a style handle (consumed; may be null). The style lives on while
//     views or other handles hold it.
//   elg_mod_<name>_style(v, style): sets the style of this family's controls inside `v`, as
//     View::button_style() and the other *_style() modifiers do. Both are borrowed; the view keeps its
//     own reference to the style. Returns the new view, owned by the caller, or null on failure.
#define STYLE_HANDLE(name, Style)                                                                                    \
    ELG_EXPORT std::shared_ptr<const Style>* elg_##name##_style_clone(const std::shared_ptr<const Style>* s) {       \
        ELG_TRY return new std::shared_ptr<const Style>(*s);                                                         \
        ELG_CATCH(nullptr)                                                                                           \
    }                                                                                                                \
    ELG_EXPORT void elg_##name##_style_free(std::shared_ptr<const Style>* s) {                                       \
        ELG_TRY delete s;                                                                                            \
        ELG_CATCH_VOID                                                                                               \
    }                                                                                                                \
    ELG_EXPORT View* elg_mod_##name##_style(const View* v, const std::shared_ptr<const Style>* style) {             \
        ELG_TRY return out(v->modifier(Style##Modifier(*style)));                                                    \
        ELG_CATCH(nullptr)                                                                                           \
    }

// The macros' arguments are names and types, which can't be parenthesized.
// NOLINTBEGIN(bugprone-macro-parentheses)

// CONFIG_HANDLE(name, Config) defines:
//   elg_<name>_config_clone(c): copies a configuration (borrowed), e.g. to keep one past the call that
//     got it. Returns the copy, owned by the caller, or null on failure.
//   elg_<name>_config_free(c): frees a configuration (consumed; may be null): one a Rust style's
//     make_body got, or a copy.
#define CONFIG_HANDLE(name, Config)                                                                                  \
    ELG_EXPORT Config* elg_##name##_config_clone(const Config* c) {                                                  \
        ELG_TRY return new Config(*c);                                                                               \
        ELG_CATCH(nullptr)                                                                                           \
    }                                                                                                                \
    ELG_EXPORT void elg_##name##_config_free(Config* c) {                                                            \
        ELG_TRY delete c;                                                                                            \
        ELG_CATCH_VOID                                                                                               \
    }

// MAKE_BODY(name, Style, Config) defines elg_<name>_style_make_body(s, c): the style's make_body(),
// its body for configuration `c`, so a Rust style can wrap or reuse another style (e.g. the one in the
// environment). Call it from a style's make_body, while Environment<K>() is available. `s` and `c` are
// borrowed. Returns the body, owned by the caller, or null on failure.
#define MAKE_BODY(name, Style, Config)                                                                               \
    ELG_EXPORT View* elg_##name##_style_make_body(const std::shared_ptr<const Style>* s, const Config* c) {         \
        ELG_TRY return out((*s)->make_body(*c));                                                                     \
        ELG_CATCH(nullptr)                                                                                           \
    }

// RUST_STYLE(name, Style, Config) defines elg_<name>_style_rust(make_body): a style implemented in
// Rust, for families whose style has only make_body(). It calls `make_body` (required: none is a usage
// error) once per control as it's built, with args: a heap copy of the configuration, owned by the Rust
// side (free it with elg_<name>_config_free), ret: View*. Returns the style, owned by the caller (free it
// with elg_<name>_style_free), or null on failure.
#define RUST_STYLE(name, Style, Config)                                                                              \
    ELG_EXPORT std::shared_ptr<const Style>* elg_##name##_style_rust(ElgClosure make_body) {                         \
        ELG_TRY const Consumed consumed(make_body);                                                                  \
        return make_style<Style, RustStyle<Style, Config>>(required(make_body, "make_body"));                        \
        ELG_CATCH(nullptr)                                                                                           \
    }

// NOLINTEND(bugprone-macro-parentheses)

#define STATE(which, member)                                                                                         \
    case which: return c->member()

// --- Buttons ---

STYLE_HANDLE(button, ButtonStyle)
CONFIG_HANDLE(button, ButtonConfiguration)
MAKE_BODY(button, ButtonStyle, ButtonConfiguration)
RUST_STYLE(button, ButtonStyle, ButtonConfiguration)

/// A built-in ButtonStyle.
///
/// @param kind 0 `ProminentButtonStyle` (filled with the tint; the default), 1 `BorderedButtonStyle`, anything
///   else `PlainButtonStyle`.
/// @return The style, owned by the caller (free it with elg_button_style_free()). Null on failure.
ELG_EXPORT std::shared_ptr<const ButtonStyle>* elg_button_style_builtin(int32_t kind) {
    ELG_TRY
    switch (kind) {
    case 0: return make_style<ButtonStyle, ProminentButtonStyle>();
    case 1: return make_style<ButtonStyle, BorderedButtonStyle>();
    default: return make_style<ButtonStyle, PlainButtonStyle>();
    }
    ELG_CATCH(nullptr)
}
/// The button's label (`ButtonConfiguration::label`).
///
/// @param c The configuration (borrowed).
/// @return A copy of the label, owned by the caller. Null on failure.
ELG_EXPORT View* elg_button_config_label(const ButtonConfiguration* c) {
    ELG_TRY
    return new View(c->label);
    ELG_CATCH(nullptr)
}
/// One of the button's states (`ButtonConfiguration`): a tracked read, so read it inside reactive arguments.
///
/// @param c The configuration (borrowed).
/// @param which 0 pressed (with the pointer still over it), 1 hovered, 2 enabled, anything else focused.
/// @return The state; false on failure.
ELG_EXPORT bool elg_button_config_state(const ButtonConfiguration* c, int32_t which) {
    ELG_TRY
    switch (which) {
        STATE(0, is_pressed);
        STATE(1, is_hovered);
        STATE(2, is_enabled);
    default: return c->is_focused();
    }
    ELG_CATCH(false)
}

// --- Toggles ---

STYLE_HANDLE(toggle, ToggleStyle)
CONFIG_HANDLE(toggle, ToggleConfiguration)
MAKE_BODY(toggle, ToggleStyle, ToggleConfiguration)

/// A ToggleStyle implemented in Rust: its `make_body()` calls `make_body`.
///
/// @param make_body Builds a toggle's body, once per toggle as it's built. args: a heap copy of the
///   configuration (ToggleConfiguration*, owned by the Rust side: free it with elg_toggle_config_free()), ret:
///   View*. Required: none is a usage error.
/// @param animation How a flip animates (borrowed; copied); null: it flips at once.
/// @return The style, owned by the caller (free it with elg_toggle_style_free()), or null on failure.
ELG_EXPORT std::shared_ptr<const ToggleStyle>* elg_toggle_style_rust(ElgClosure make_body, const Animation* animation) {
    ELG_TRY
    const Consumed consumed(make_body);
    return make_style<ToggleStyle, RustAnimatedStyle<ToggleStyle, ToggleConfiguration>>(
        required(make_body, "make_body"), elg_shim::animation(animation));
    ELG_CATCH(nullptr)
}

/// The options of elg_switch_toggle_style(): `SwitchToggleStyleOptions`.
struct ElgSwitchToggleStyleOptions {
    const ElgProp* on; ///< A Color prop: the track while on; null: the tint.
    ElgProp off;       ///< A Color prop: the track while off.
    ElgProp knob;      ///< A Color prop: the knob.
    /// The switch's width and height, the gap between the knob and the track's edge, and the space between the
    /// label and the switch, in points.
    float width, height, knob_inset, spacing;
    /// The label at the leading edge and the switch at the trailing one, across the width it's offered (a
    /// settings row); false: as wide as the label and the switch.
    bool fills_width;
    const Animation* animation; ///< How a flip animates (borrowed); null: it flips at once.
};

/// Wraps `SwitchToggleStyle`: the label, then a sliding switch in the tint. The default ToggleStyle.
///
/// @param o The options (borrowed; their closures are taken over).
/// @return The style, owned by the caller (free it with elg_toggle_style_free()), or null on failure.
ELG_EXPORT std::shared_ptr<const ToggleStyle>* elg_switch_toggle_style(const ElgSwitchToggleStyleOptions* o) {
    ELG_TRY
    ElgSwitchToggleStyleOptions c = *o;
    ElgProp on = c.on ? *c.on : ElgProp{};
    const Consumed consumed(on, c.off, c.knob);
    SwitchToggleStyleOptions options;
    options.on = optional_color(c.on, on);
    options.off = prop<Color>(c.off);
    options.knob = prop<Color>(c.knob);
    options.width = o->width;
    options.height = o->height;
    options.knob_inset = o->knob_inset;
    options.spacing = o->spacing;
    options.fills_width = o->fills_width;
    options.animation = animation(o->animation);
    return make_style<ToggleStyle, SwitchToggleStyle>(std::move(options));
    ELG_CATCH(nullptr)
}
/// The toggle's label (`ToggleConfiguration::label`).
///
/// @param c The configuration (borrowed).
/// @return A copy of the label, owned by the caller. Null on failure.
ELG_EXPORT View* elg_toggle_config_label(const ToggleConfiguration* c) {
    ELG_TRY
    return new View(c->label);
    ELG_CATCH(nullptr)
}
/// One of the toggle's states (`ToggleConfiguration`): a tracked read, so read it inside reactive arguments.
///
/// @param c The configuration (borrowed).
/// @param which 0 on, 1 pressed, 2 hovered, 3 enabled, anything else focused.
/// @return The state; false on failure.
ELG_EXPORT bool elg_toggle_config_state(const ToggleConfiguration* c, int32_t which) {
    ELG_TRY
    switch (which) {
        STATE(0, is_on);
        STATE(1, is_pressed);
        STATE(2, is_hovered);
        STATE(3, is_enabled);
    default: return c->is_focused();
    }
    ELG_CATCH(false)
}
/// Flips the toggle, with the style's animation (`ToggleConfiguration::toggle`).
///
/// @param c The configuration (borrowed).
ELG_EXPORT void elg_toggle_config_toggle(const ToggleConfiguration* c) {
    ELG_TRY
    c->toggle();
    ELG_CATCH_VOID
}

// --- Check boxes ---

STYLE_HANDLE(checkbox, CheckboxStyle)
CONFIG_HANDLE(checkbox, CheckboxConfiguration)
MAKE_BODY(checkbox, CheckboxStyle, CheckboxConfiguration)

/// A CheckboxStyle implemented in Rust: its `make_body()` calls `make_body`.
///
/// @param make_body Builds a check box's body, once per check box as it's built. args: a heap copy of the
///   configuration (CheckboxConfiguration*, owned by the Rust side: free it with elg_checkbox_config_free()),
///   ret: View*. Required: none is a usage error.
/// @param animation How checking and unchecking animate (borrowed; copied); null: at once.
/// @return The style, owned by the caller (free it with elg_checkbox_style_free()), or null on failure.
ELG_EXPORT std::shared_ptr<const CheckboxStyle>* elg_checkbox_style_rust(ElgClosure make_body, const Animation* animation) {
    ELG_TRY
    const Consumed consumed(make_body);
    return make_style<CheckboxStyle, RustAnimatedStyle<CheckboxStyle, CheckboxConfiguration>>(
        required(make_body, "make_body"), elg_shim::animation(animation));
    ELG_CATCH(nullptr)
}

/// The options of elg_box_checkbox_style(): `BoxCheckboxStyleOptions`.
struct ElgBoxCheckboxStyleOptions {
    float size, corner_radius;  ///< The box's size and corner radius, in points.
    const ElgProp* fill;        ///< A Color prop: the box while checked or mixed; null: the tint.
    ElgProp empty;              ///< A Color prop: the box while unchecked.
    ElgProp border;             ///< A Color prop: the outline while unchecked (checked or mixed, it's the fill).
    float border_width;         ///< The outline's width, in points.
    ElgProp mark;               ///< A Color prop: the check and the dash.
    View* take_check;           ///< Shown while checked (consumed); null: a check mark.
    View* take_dash;            ///< Shown while mixed (consumed); null: a dash.
    float spacing;              ///< Between the box and the label, in points.
    const Animation* animation; ///< How checking and unchecking animate (borrowed); null: at once.
};

/// Wraps `BoxCheckboxStyle`: a box, then the label; filled in the tint with a check mark while checked, a dash
/// while mixed. The default CheckboxStyle.
///
/// @param o The options (borrowed; their closures are taken over, and their views consumed).
/// @return The style, owned by the caller (free it with elg_checkbox_style_free()), or null on failure.
ELG_EXPORT std::shared_ptr<const CheckboxStyle>* elg_box_checkbox_style(const ElgBoxCheckboxStyleOptions* o) {
    ELG_TRY
    ElgBoxCheckboxStyleOptions c = *o;
    ElgProp fill = c.fill ? *c.fill : ElgProp{};
    const Consumed consumed(fill, c.empty, c.border, c.mark, c.take_check, c.take_dash);
    BoxCheckboxStyleOptions options;
    options.size = c.size;
    options.corner_radius = c.corner_radius;
    options.fill = optional_color(c.fill, fill);
    options.empty = prop<Color>(c.empty);
    options.border = prop<Color>(c.border);
    options.border_width = c.border_width;
    options.mark = prop<Color>(c.mark);
    if (c.take_check) {
        options.check = take(c.take_check);
    }
    if (c.take_dash) {
        options.dash = take(c.take_dash);
    }
    options.spacing = c.spacing;
    options.animation = animation(c.animation);
    return make_style<CheckboxStyle, BoxCheckboxStyle>(std::move(options));
    ELG_CATCH(nullptr)
}
/// The check box's label (`CheckboxConfiguration::label`).
///
/// @param c The configuration (borrowed).
/// @return A copy of the label, owned by the caller. Null on failure.
ELG_EXPORT View* elg_checkbox_config_label(const CheckboxConfiguration* c) {
    ELG_TRY
    return new View(c->label);
    ELG_CATCH(nullptr)
}
/// One of the check box's states (`CheckboxConfiguration`): a tracked read, so read it inside reactive
/// arguments.
///
/// @param c The configuration (borrowed).
/// @param which 0 checked, 1 mixed, 2 pressed, 3 hovered, 4 enabled, anything else focused.
/// @return The state; false on failure.
ELG_EXPORT bool elg_checkbox_config_state(const CheckboxConfiguration* c, int32_t which) {
    ELG_TRY
    switch (which) {
        STATE(0, is_checked);
        STATE(1, is_mixed);
        STATE(2, is_pressed);
        STATE(3, is_hovered);
        STATE(4, is_enabled);
    default: return c->is_focused();
    }
    ELG_CATCH(false)
}
/// Checks or unchecks the check box, with the style's animation; a mixed one becomes checked
/// (`CheckboxConfiguration::toggle`).
///
/// @param c The configuration (borrowed).
ELG_EXPORT void elg_checkbox_config_toggle(const CheckboxConfiguration* c) {
    ELG_TRY
    c->toggle();
    ELG_CATCH_VOID
}

// --- Number inputs ---

STYLE_HANDLE(number_input, NumberInputStyle)
CONFIG_HANDLE(number_input, NumberInputConfiguration)
MAKE_BODY(number_input, NumberInputStyle, NumberInputConfiguration)
RUST_STYLE(number_input, NumberInputStyle, NumberInputConfiguration)

/// The options of a `BoxNumberInputStyle` (`BoxNumberInputStyleOptions`), for elg_number_input_style_builtin().
struct ElgBoxNumberInputStyleOptions {
    ElgProp background;                ///< A Color prop: the box.
    ElgProp border;                    ///< A Color prop: the outline.
    const ElgProp* focused_border;     ///< A Color prop: the outline while editing; null: the tint.
    float border_width, corner_radius; ///< The outline's width and the box's corner radius, in points.
    ElgEdgeInsets padding;             ///< Around the number, inside the box.
    float arrow_size;                  ///< The stacked arrows' icons, in points.
};

/// A built-in NumberInputStyle.
///
/// @param o `BoxNumberInputStyle`'s options (borrowed; their closures are taken over): a raised box with an
///   outline and stacked arrows, the default. Null: `PlainNumberInputStyle`, just the number and the arrows.
/// @return The style, owned by the caller (free it with elg_number_input_style_free()), or null on failure.
ELG_EXPORT std::shared_ptr<const NumberInputStyle>* elg_number_input_style_builtin(const ElgBoxNumberInputStyleOptions* o) {
    ELG_TRY
    if (!o) {
        return make_style<NumberInputStyle, PlainNumberInputStyle>();
    }
    ElgBoxNumberInputStyleOptions c = *o;
    ElgProp focused_border = c.focused_border ? *c.focused_border : ElgProp{};
    const Consumed consumed(c.background, c.border, focused_border);
    BoxNumberInputStyleOptions options;
    options.background = prop<Color>(c.background);
    options.border = prop<Color>(c.border);
    options.focused_border = optional_color(c.focused_border, focused_border);
    options.border_width = c.border_width;
    options.corner_radius = c.corner_radius;
    options.padding = edge_insets(c.padding);
    options.arrow_size = c.arrow_size;
    return make_style<NumberInputStyle, BoxNumberInputStyle>(std::move(options));
    ELG_CATCH(nullptr)
}
/// The editable number (`NumberInputConfiguration::field`): put it where the text goes.
///
/// @param c The configuration (borrowed).
/// @return A copy of the field, owned by the caller. Null on failure.
ELG_EXPORT View* elg_number_input_config_field(const NumberInputConfiguration* c) {
    ELG_TRY
    return new View(c->field);
    ELG_CATCH(nullptr)
}
/// One of the number input's states (`NumberInputConfiguration`): a tracked read, so read it inside reactive
/// arguments.
///
/// @param c The configuration (borrowed).
/// @param which 0 can_increment (below the maximum), 1 can_decrement (above the minimum), 2 shows_buttons,
///   3 focused (being edited), 4 hovered, anything else enabled.
/// @return The state; false on failure.
ELG_EXPORT bool elg_number_input_config_state(const NumberInputConfiguration* c, int32_t which) {
    ELG_TRY
    switch (which) {
        STATE(0, can_increment);
        STATE(1, can_decrement);
        STATE(2, shows_buttons);
        STATE(3, is_focused);
        STATE(4, is_hovered);
    default: return c->is_enabled();
    }
    ELG_CATCH(false)
}
/// Steps the number input's value, within its range (`NumberInputConfiguration::increment` and `decrement`).
///
/// @param c The configuration (borrowed).
/// @param which 0 a step up, anything else a step down.
ELG_EXPORT void elg_number_input_config_action(const NumberInputConfiguration* c, int32_t which) {
    ELG_TRY
    if (which == 0) {
        c->increment();
    } else {
        c->decrement();
    }
    ELG_CATCH_VOID
}

// --- Progress ---

STYLE_HANDLE(progress, ProgressStyle)
CONFIG_HANDLE(progress, ProgressConfiguration)
MAKE_BODY(progress, ProgressStyle, ProgressConfiguration)
RUST_STYLE(progress, ProgressStyle, ProgressConfiguration)

/// The options of elg_progress_style_builtin(): `LinearProgressStyleOptions` or
/// `CircularProgressStyleOptions`.
struct ElgProgressStyleOptions {
    ElgProp track;       ///< A Color prop: the track.
    const ElgProp* fill; ///< A Color prop: the fill; null: the tint.
    float a;             ///< Linear: the bar's thickness; circular: the ring's diameter. In points.
    float b;             ///< Circular: the ring's line width, in points; unused by the linear style.
};

/// A built-in ProgressStyle.
///
/// @param kind 0 `LinearProgressStyle` (a bar as wide as offered; the default), anything else
///   `CircularProgressStyle` (a ring).
/// @param o The style's options (borrowed; required; their closures are taken over).
/// @return The style, owned by the caller (free it with elg_progress_style_free()), or null on failure.
ELG_EXPORT std::shared_ptr<const ProgressStyle>* elg_progress_style_builtin(int32_t kind, const ElgProgressStyleOptions* o) {
    ELG_TRY
    ElgProgressStyleOptions c = *o;
    ElgProp fill = c.fill ? *c.fill : ElgProp{};
    const Consumed consumed(c.track, fill);
    if (kind == 0) {
        return make_style<ProgressStyle, LinearProgressStyle>(
            LinearProgressStyleOptions{prop<Color>(c.track), optional_color(c.fill, fill), c.a});
    }
    return make_style<ProgressStyle, CircularProgressStyle>(
        CircularProgressStyleOptions{prop<Color>(c.track), optional_color(c.fill, fill), c.a, c.b});
    ELG_CATCH(nullptr)
}
/// How far along the progress is (`ProgressConfiguration::fraction`): a tracked read.
///
/// @param c The configuration (borrowed).
/// @return The fraction, 0...1; `has` false while indeterminate, and on failure.
ELG_EXPORT ElgOptLen elg_progress_config_fraction(const ProgressConfiguration* c) {
    ELG_TRY
    const std::optional<float> fraction = c->fraction();
    return {fraction.has_value(), fraction.value_or(0)};
    ELG_CATCH(ElgOptLen{})
}

// --- Text fields ---

STYLE_HANDLE(text_field, TextFieldStyle)
CONFIG_HANDLE(text_field, TextFieldConfiguration)
MAKE_BODY(text_field, TextFieldStyle, TextFieldConfiguration)
RUST_STYLE(text_field, TextFieldStyle, TextFieldConfiguration)

/// A built-in TextFieldStyle.
///
/// @param kind 0 `RoundedBorderTextFieldStyle` (a raised box with an outline; the default), anything else
///   `PlainTextFieldStyle` (just the text).
/// @return The style, owned by the caller (free it with elg_text_field_style_free()). Null on failure.
ELG_EXPORT std::shared_ptr<const TextFieldStyle>* elg_text_field_style_builtin(int32_t kind) {
    ELG_TRY
    if (kind == 0) {
        return make_style<TextFieldStyle, RoundedBorderTextFieldStyle>();
    }
    return make_style<TextFieldStyle, PlainTextFieldStyle>();
    ELG_CATCH(nullptr)
}
/// The editable text (`TextFieldConfiguration::field`): put it where the text goes.
///
/// @param c The configuration (borrowed).
/// @return A copy of the field, owned by the caller. Null on failure.
ELG_EXPORT View* elg_text_field_config_field(const TextFieldConfiguration* c) {
    ELG_TRY
    return new View(c->field);
    ELG_CATCH(nullptr)
}
/// One of the text field's states (`TextFieldConfiguration`): a tracked read, so read it inside reactive
/// arguments.
///
/// @param c The configuration (borrowed).
/// @param which 0 focused, 1 hovered, anything else enabled.
/// @return The state; false on failure.
ELG_EXPORT bool elg_text_field_config_state(const TextFieldConfiguration* c, int32_t which) {
    ELG_TRY
    switch (which) {
        STATE(0, is_focused);
        STATE(1, is_hovered);
    default: return c->is_enabled();
    }
    ELG_CATCH(false)
}

// --- Pickers ---

STYLE_HANDLE(picker, PickerStyle)
CONFIG_HANDLE(picker, PickerConfiguration)
MAKE_BODY(picker, PickerStyle, PickerConfiguration)
RUST_STYLE(picker, PickerStyle, PickerConfiguration)

/// A built-in PickerStyle.
///
/// @param kind 0 `SegmentedPickerStyle` (segments in a rounded track; the default), anything else
///   `MenuPickerStyle` (a button that opens a menu of the options).
/// @return The style, owned by the caller (free it with elg_picker_style_free()). Null on failure.
ELG_EXPORT std::shared_ptr<const PickerStyle>* elg_picker_style_builtin(int32_t kind) {
    ELG_TRY
    if (kind == 0) {
        return make_style<PickerStyle, SegmentedPickerStyle>();
    }
    return make_style<PickerStyle, MenuPickerStyle>();
    ELG_CATCH(nullptr)
}
/// How many options the picker has (`PickerConfiguration::labels`).
///
/// @param c The configuration (borrowed).
/// @return The number of options. 0 on failure.
ELG_EXPORT size_t elg_picker_config_count(const PickerConfiguration* c) {
    ELG_TRY
    return c->labels.size();
    ELG_CATCH(0)
}
/// An option's label (`PickerConfiguration::labels`).
///
/// @param c The configuration (borrowed).
/// @param index The option's index.
/// @return The label, borrowed from the configuration; empty on failure: a UsageError when `index` is past the
///   last label.
ELG_EXPORT ElgStr elg_picker_config_label(const PickerConfiguration* c, size_t index) {
    ELG_TRY
    if (index >= c->labels.size()) {
        throw elg::UsageError("elg_picker_config_label: the index is past the last label");
    }
    return c_str(c->labels[index]);
    ELG_CATCH(ElgStr{})
}
/// The chosen option (`PickerConfiguration::selected`): a tracked read.
///
/// @param c The configuration (borrowed).
/// @return The chosen option's index; the number of options while none is chosen, and on failure.
ELG_EXPORT size_t elg_picker_config_selected(const PickerConfiguration* c) {
    ELG_TRY
    return c->selected();
    ELG_CATCH(c->labels.size())
}
/// Chooses an option (`PickerConfiguration::select`).
///
/// @param c The configuration (borrowed).
/// @param index The option's index.
ELG_EXPORT void elg_picker_config_select(const PickerConfiguration* c, size_t index) {
    ELG_TRY
    c->select(index);
    ELG_CATCH_VOID
}

// --- Sliders ---

STYLE_HANDLE(slider, SliderStyle)
CONFIG_HANDLE(slider, SliderConfiguration)

/// A SliderStyle implemented in Rust: three parts, which the slider places itself. Each builder runs once per
/// slider as it's built. args: a heap copy of the configuration (SliderConfiguration*, owned by the Rust side:
/// free it with elg_slider_config_free()), ret: View*.
///
/// @param track The track (`make_track()`), offered the slider's length less the thumb's width. Required:
///   none is a usage error.
/// @param fill Over the track from its start to the thumb's center (`make_fill()`); none: nothing.
/// @param thumb The thumb (`make_thumb()`), at its ideal size, centered on the value. Required: none is a
///   usage error.
/// @return The style, owned by the caller (free it with elg_slider_style_free()), or null on failure.
ELG_EXPORT std::shared_ptr<const SliderStyle>* elg_slider_style_rust(ElgClosure track, ElgClosure fill, ElgClosure thumb) {
    ELG_TRY
    const Consumed consumed(track, fill, thumb);
    SharedFn track_fn = required(track, "track");
    SharedFn fill_fn = share(fill);
    SharedFn thumb_fn = required(thumb, "thumb");
    return make_style<SliderStyle, RustSliderStyle>(std::move(track_fn), std::move(fill_fn), std::move(thumb_fn));
    ELG_CATCH(nullptr)
}

/// The options of elg_knob_slider_style(): `KnobSliderStyleOptions`.
struct ElgKnobSliderStyleOptions {
    ElgProp track;         ///< A Color prop: the track.
    const ElgProp* fill;   ///< A Color prop: the track up to the knob; null: the tint.
    float track_thickness; ///< The track's thickness, in points.
    float knob_size;       ///< The knob's diameter, in points.
};

/// Wraps `KnobSliderStyle`: a thin capsule track, filled in the tint up to a round knob. The default
/// SliderStyle.
///
/// @param o The options (borrowed; their closures are taken over).
/// @return The style, owned by the caller (free it with elg_slider_style_free()), or null on failure.
ELG_EXPORT std::shared_ptr<const SliderStyle>* elg_knob_slider_style(const ElgKnobSliderStyleOptions* o) {
    ELG_TRY
    ElgKnobSliderStyleOptions c = *o;
    ElgProp fill = c.fill ? *c.fill : ElgProp{};
    const Consumed consumed(c.track, fill);
    return make_style<SliderStyle, KnobSliderStyle>(
        KnobSliderStyleOptions{prop<Color>(c.track), optional_color(c.fill, fill), c.track_thickness, c.knob_size});
    ELG_CATCH(nullptr)
}
/// Wraps the style's `make_track()`, `make_fill()` or `make_thumb()`, so a Rust style can reuse another style's
/// parts.
///
/// @param s The style (borrowed).
/// @param c The configuration (borrowed).
/// @param part 0 the track, 1 the fill, anything else the thumb.
/// @return The part, owned by the caller, or null on failure.
ELG_EXPORT View* elg_slider_style_make_part(const std::shared_ptr<const SliderStyle>* s, const SliderConfiguration* c,
                                            int32_t part) {
    ELG_TRY
    switch (part) {
    case 0: return out((*s)->make_track(*c));
    case 1: return out((*s)->make_fill(*c));
    default: return out((*s)->make_thumb(*c));
    }
    ELG_CATCH(nullptr)
}
/// Where the slider's value is between its minimum and maximum (`SliderConfiguration::fraction`): a tracked
/// read.
///
/// @param c The configuration (borrowed).
/// @return The fraction, 0...1; 0 on failure.
ELG_EXPORT float elg_slider_config_fraction(const SliderConfiguration* c) {
    ELG_TRY
    return c->fraction();
    ELG_CATCH(0.f)
}
/// One of the slider's states (`SliderConfiguration`): a tracked read, so read it inside reactive arguments.
///
/// @param c The configuration (borrowed).
/// @param which 0 pressed (being dragged), 1 hovered, 2 enabled, anything else focused.
/// @return The state; false on failure.
ELG_EXPORT bool elg_slider_config_state(const SliderConfiguration* c, int32_t which) {
    ELG_TRY
    switch (which) {
        STATE(0, is_pressed);
        STATE(1, is_hovered);
        STATE(2, is_enabled);
    default: return c->is_focused();
    }
    ELG_CATCH(false)
}

// --- Navigation stacks ---

STYLE_HANDLE(navigation_stack, NavigationStackStyle)
CONFIG_HANDLE(navigation_stack, NavigationStackConfiguration)
MAKE_BODY(navigation_stack, NavigationStackStyle, NavigationStackConfiguration)
RUST_STYLE(navigation_stack, NavigationStackStyle, NavigationStackConfiguration)

/// The options of a `BarNavigationStackStyle` (`BarNavigationStackStyleOptions`), for
/// elg_navigation_stack_style_builtin().
struct ElgBarNavigationStackStyleOptions {
    float height;              ///< The bar's height, in points.
    ElgProp background;        ///< A Color prop: behind the bar.
    ElgFont title_font;        ///< The title's font.
    const ElgProp* back_color; ///< A Color prop: the back button; null: the tint.
    bool shows_divider;        ///< A line under the bar.
};

/// A built-in NavigationStackStyle.
///
/// @param o `BarNavigationStackStyle`'s options (borrowed; their closures are taken over): a bar over the
///   pages with the top page's title and, once a page is pushed, a back button. Null: `PlainNavigationStackStyle`,
///   just the pages (the default).
/// @return The style, owned by the caller (free it with elg_navigation_stack_style_free()), or null on failure.
ELG_EXPORT std::shared_ptr<const NavigationStackStyle>* elg_navigation_stack_style_builtin(
    const ElgBarNavigationStackStyleOptions* o) {
    ELG_TRY
    if (!o) {
        return make_style<NavigationStackStyle, PlainNavigationStackStyle>();
    }
    ElgBarNavigationStackStyleOptions c = *o;
    ElgProp back_color = c.back_color ? *c.back_color : ElgProp{};
    const Consumed consumed(c.background, back_color);
    BarNavigationStackStyleOptions options;
    options.height = c.height;
    options.background = prop<Color>(c.background);
    options.title_font = font(c.title_font);
    options.back_color = optional_color(c.back_color, back_color);
    options.shows_divider = c.shows_divider;
    return make_style<NavigationStackStyle, BarNavigationStackStyle>(std::move(options));
    ELG_CATCH(nullptr)
}
/// The pages (`NavigationStackConfiguration::pages`): put them where they go.
///
/// @param c The configuration (borrowed).
/// @return A copy of the pages view, owned by the caller. Null on failure.
ELG_EXPORT View* elg_navigation_stack_config_pages(const NavigationStackConfiguration* c) {
    ELG_TRY
    return new View(c->pages);
    ELG_CATCH(nullptr)
}
/// The pages' titles (`NavigationStackConfiguration::titles`): a tracked read.
///
/// @param c The configuration (borrowed).
/// @param each Called with each title, the root's first, during this call.
/// @param ctx Passed to `each`.
ELG_EXPORT void elg_navigation_stack_config_titles(const NavigationStackConfiguration* c, ElgStrSink each, void* ctx) {
    ELG_TRY
    for (const std::string& title : c->titles()) {
        sink(each, ctx, title);
    }
    ELG_CATCH_VOID
}
/// Whether a page is pushed (`NavigationStackConfiguration::can_go_back`): a tracked read.
///
/// @param c The configuration (borrowed).
/// @return Whether there's a page to go back from; false on failure.
ELG_EXPORT bool elg_navigation_stack_config_can_go_back(const NavigationStackConfiguration* c) {
    ELG_TRY
    return c->can_go_back();
    ELG_CATCH(false)
}
/// Pops the top page (`NavigationStackConfiguration::back`).
///
/// @param c The configuration (borrowed).
ELG_EXPORT void elg_navigation_stack_config_back(const NavigationStackConfiguration* c) {
    ELG_TRY
    c->back();
    ELG_CATCH_VOID
}

// --- Popovers ---

STYLE_HANDLE(popover, PopoverStyle)
CONFIG_HANDLE(popover, PopoverConfiguration)
MAKE_BODY(popover, PopoverStyle, PopoverConfiguration)

/// A PopoverStyle implemented in Rust: its `make_body()` calls `make_body`.
///
/// @param make_body Wraps the content in a panel, each time a popover opens. args: a heap copy of the
///   configuration (PopoverConfiguration*, owned by the Rust side: free it with elg_popover_config_free()),
///   ret: View*. Required: none is a usage error.
/// @param popup_motion How popovers open and close (`motion()`); `has` false: at once.
/// @return The style, owned by the caller (free it with elg_popover_style_free()), or null on failure.
ELG_EXPORT std::shared_ptr<const PopoverStyle>* elg_popover_style_rust(ElgClosure make_body, ElgPopupMotion popup_motion) {
    ELG_TRY
    const Consumed consumed(make_body);
    return make_style<PopoverStyle, RustPopupStyle<PopoverStyle, PopoverConfiguration>>(
        required(make_body, "make_body"), motion(popup_motion));
    ELG_CATCH(nullptr)
}
/// A built-in PopoverStyle.
///
/// @param kind 0 `DefaultPopoverStyle` (a rounded surface with an outline and a shadow), anything else
///   `PlainPopoverStyle` (just the content).
/// @return The style, owned by the caller (free it with elg_popover_style_free()). Null on failure.
ELG_EXPORT std::shared_ptr<const PopoverStyle>* elg_popover_style_builtin(int32_t kind) {
    ELG_TRY
    if (kind == 0) {
        return make_style<PopoverStyle, DefaultPopoverStyle>();
    }
    return make_style<PopoverStyle, PlainPopoverStyle>();
    ELG_CATCH(nullptr)
}
/// The popover's content (`PopoverConfiguration::content`).
///
/// @param c The configuration (borrowed).
/// @return A copy of the content, owned by the caller. Null on failure.
ELG_EXPORT View* elg_popover_config_content(const PopoverConfiguration* c) {
    ELG_TRY
    return new View(c->content);
    ELG_CATCH(nullptr)
}
/// Whether to pad the content (`PopoverConfiguration::is_padded`, from `PopoverOptions::padded`): a tracked
/// read. false lets the content reach the panel's edges.
///
/// @param c The configuration (borrowed).
/// @return Whether it's padded; true on failure.
ELG_EXPORT bool elg_popover_config_is_padded(const PopoverConfiguration* c) {
    ELG_TRY
    return c->is_padded();
    ELG_CATCH(true)
}

// --- Tooltips ---

STYLE_HANDLE(tooltip, TooltipStyle)
CONFIG_HANDLE(tooltip, TooltipConfiguration)
MAKE_BODY(tooltip, TooltipStyle, TooltipConfiguration)

/// A TooltipStyle implemented in Rust: its `make_body()` calls `make_body`.
///
/// @param make_body Wraps the content, each time a tooltip opens. args: a heap copy of the configuration
///   (TooltipConfiguration*, owned by the Rust side: free it with elg_tooltip_config_free()), ret: View*.
///   Required: none is a usage error.
/// @param popup_motion How tooltips open and close (`motion()`); `has` false: at once.
/// @return The style, owned by the caller (free it with elg_tooltip_style_free()), or null on failure.
ELG_EXPORT std::shared_ptr<const TooltipStyle>* elg_tooltip_style_rust(ElgClosure make_body, ElgPopupMotion popup_motion) {
    ELG_TRY
    const Consumed consumed(make_body);
    return make_style<TooltipStyle, RustPopupStyle<TooltipStyle, TooltipConfiguration>>(
        required(make_body, "make_body"), motion(popup_motion));
    ELG_CATCH(nullptr)
}
/// A built-in TooltipStyle.
///
/// @param kind 0 `DefaultTooltipStyle` (small text on a rounded surface), anything else `PlainTooltipStyle`
///   (just the content).
/// @return The style, owned by the caller (free it with elg_tooltip_style_free()). Null on failure.
ELG_EXPORT std::shared_ptr<const TooltipStyle>* elg_tooltip_style_builtin(int32_t kind) {
    ELG_TRY
    if (kind == 0) {
        return make_style<TooltipStyle, DefaultTooltipStyle>();
    }
    return make_style<TooltipStyle, PlainTooltipStyle>();
    ELG_CATCH(nullptr)
}
/// The tooltip's content (`TooltipConfiguration::content`); for a text tooltip, the text.
///
/// @param c The configuration (borrowed).
/// @return A copy of the content, owned by the caller. Null on failure.
ELG_EXPORT View* elg_tooltip_config_content(const TooltipConfiguration* c) {
    ELG_TRY
    return new View(c->content);
    ELG_CATCH(nullptr)
}

// --- Menus ---

STYLE_HANDLE(menu, MenuStyle)
CONFIG_HANDLE(menu, MenuConfiguration)
CONFIG_HANDLE(menu_item, MenuItemConfiguration)
MAKE_BODY(menu, MenuStyle, MenuConfiguration)

/// A MenuStyle implemented in Rust: its `make_body()` and `make_item()` call `make_body` and `make_item`.
///
/// @param make_body Wraps the items in a panel, each time a menu opens. args: a heap copy of the configuration
///   (MenuConfiguration*, owned by the Rust side: free it with elg_menu_config_free()), ret: View*. Required:
///   none is a usage error.
/// @param make_item Builds a row, once per row. args: a heap copy of the row's configuration
///   (MenuItemConfiguration*, freed with elg_menu_item_config_free()), ret: View*. None: the default row.
/// @param popup_motion How menus open and close (`motion()`); `has` false: at once.
/// @return The style, owned by the caller (free it with elg_menu_style_free()), or null on failure.
ELG_EXPORT std::shared_ptr<const MenuStyle>* elg_menu_style_rust(ElgClosure make_body, ElgClosure make_item,
                                                                  ElgPopupMotion popup_motion) {
    ELG_TRY
    const Consumed consumed(make_body, make_item);
    SharedFn body = required(make_body, "make_body");
    SharedFn item = share(make_item);
    return make_style<MenuStyle, RustMenuStyle>(std::move(body), std::move(item), motion(popup_motion));
    ELG_CATCH(nullptr)
}
/// A built-in MenuStyle.
///
/// @param kind 0 `DefaultMenuStyle` (a surface with an outline and a shadow), anything else `PlainMenuStyle`
///   (just the items).
/// @return The style, owned by the caller (free it with elg_menu_style_free()). Null on failure.
ELG_EXPORT std::shared_ptr<const MenuStyle>* elg_menu_style_builtin(int32_t kind) {
    ELG_TRY
    if (kind == 0) {
        return make_style<MenuStyle, DefaultMenuStyle>();
    }
    return make_style<MenuStyle, PlainMenuStyle>();
    ELG_CATCH(nullptr)
}
/// Wraps the style's `make_item()`: its row for an item's configuration, so a Rust style can reuse another
/// style's rows (by default the parts in a line, padded, on a rounded highlight).
///
/// @param s The style (borrowed).
/// @param c The row's configuration (borrowed).
/// @return The row, owned by the caller, or null on failure.
ELG_EXPORT View* elg_menu_style_make_item(const std::shared_ptr<const MenuStyle>* s, const MenuItemConfiguration* c) {
    ELG_TRY
    return out((*s)->make_item(*c));
    ELG_CATCH(nullptr)
}
/// The menu's rows, stacked (`MenuConfiguration::items`); rows span the panel's width.
///
/// @param c The configuration (borrowed).
/// @return A copy of the items view, owned by the caller. Null on failure.
ELG_EXPORT View* elg_menu_config_items(const MenuConfiguration* c) {
    ELG_TRY
    return new View(c->items);
    ELG_CATCH(nullptr)
}
/// One of a menu row's parts (`MenuItemConfiguration`), for the style to lay out.
///
/// @param c The row's configuration (borrowed).
/// @param which 0 the title; 1 the leading part: the check mark and icon, empty when the item has neither;
///   anything else the trailing part: the shortcut hint or a submenu's chevron, empty when there's neither.
/// @return A copy of the part, owned by the caller. Null on failure.
ELG_EXPORT View* elg_menu_item_config_view(const MenuItemConfiguration* c, int32_t which) {
    ELG_TRY
    switch (which) {
    case 0: return new View(c->title);
    case 1: return new View(c->leading);
    default: return new View(c->trailing);
    }
    ELG_CATCH(nullptr)
}
/// One of a menu row's states (`MenuItemConfiguration`): a tracked read, so read it inside reactive arguments.
///
/// @param c The row's configuration (borrowed).
/// @param which 0 highlighted (hovered, or focused with the keyboard), 1 enabled, anything else destructive.
/// @return The state; false on failure.
ELG_EXPORT bool elg_menu_item_config_state(const MenuItemConfiguration* c, int32_t which) {
    ELG_TRY
    switch (which) {
        STATE(0, is_highlighted);
        STATE(1, is_enabled);
    default: return c->is_destructive();
    }
    ELG_CATCH(false)
}

// --- Selectable rows ---

STYLE_HANDLE(row, RowStyle)
CONFIG_HANDLE(row, RowConfiguration)
MAKE_BODY(row, RowStyle, RowConfiguration)
RUST_STYLE(row, RowStyle, RowConfiguration)

/// The options of a `HighlightRowStyle` (`HighlightRowStyleOptions`), for elg_row_style_builtin().
struct ElgHighlightRowStyleOptions {
    const ElgProp* selected; ///< A Color prop: the selected rows' highlight; null: the tint.
    ElgProp on_selected;     ///< A Color prop: the selected rows' text and icons.
    ElgProp hovered;         ///< A Color prop: the hovered row's highlight, drawn at half opacity.
    float corner_radius;     ///< The highlight's corner radius, in points.
    ElgEdgeInsets padding;   ///< Around the content, inside the highlight.
};

/// A built-in RowStyle.
///
/// @param o `HighlightRowStyle`'s options (borrowed; their closures are taken over): the content, padded, on a
///   rounded highlight, the default. Null: `PlainRowStyle`, just the content.
/// @return The style, owned by the caller (free it with elg_row_style_free()), or null on failure.
ELG_EXPORT std::shared_ptr<const RowStyle>* elg_row_style_builtin(const ElgHighlightRowStyleOptions* o) {
    ELG_TRY
    if (!o) {
        return make_style<RowStyle, PlainRowStyle>();
    }
    ElgHighlightRowStyleOptions c = *o;
    ElgProp selected = c.selected ? *c.selected : ElgProp{};
    const Consumed consumed(selected, c.on_selected, c.hovered);
    return make_style<RowStyle, HighlightRowStyle>(HighlightRowStyleOptions{
        optional_color(c.selected, selected), prop<Color>(c.on_selected), prop<Color>(c.hovered), c.corner_radius,
        edge_insets(c.padding)});
    ELG_CATCH(nullptr)
}
/// The row's content: the ForEach item's view (`RowConfiguration::content`).
///
/// @param c The configuration (borrowed).
/// @return A copy of the content, owned by the caller. Null on failure.
ELG_EXPORT View* elg_row_config_content(const RowConfiguration* c) {
    ELG_TRY
    return new View(c->content);
    ELG_CATCH(nullptr)
}
/// The stack's axis (`RowConfiguration::axis`): rows usually span across it.
///
/// @param c The configuration (borrowed).
/// @return `Axis`: 0 horizontal, 1 vertical. 0 on failure.
ELG_EXPORT int32_t elg_row_config_axis(const RowConfiguration* c) {
    ELG_TRY
    return static_cast<int32_t>(c->axis);
    ELG_CATCH(0)
}
/// One of the row's states (`RowConfiguration`): a tracked read, so read it inside reactive arguments.
///
/// @param c The configuration (borrowed).
/// @param which 0 selected, 1 hovered, 2 focused (the row keys act on), anything else enabled.
/// @return The state; false on failure.
ELG_EXPORT bool elg_row_config_state(const RowConfiguration* c, int32_t which) {
    ELG_TRY
    switch (which) {
        STATE(0, is_selected);
        STATE(1, is_hovered);
        STATE(2, is_focused);
    default: return c->is_enabled();
    }
    ELG_CATCH(false)
}

// --- ABI layout ---

/// Reports the size and alignment of each C struct in this file, for elegance-sys's layout test (which
/// compares them with its Rust declarations).
///
/// @param out Called once per struct with a "name size align" line.
/// @param ctx Passed to `out`.
ELG_EXPORT void elg_layouts_styles(ElgStrSink out, void* ctx) {
    ELG_TRY
    ELG_LAYOUT(ElgSwitchToggleStyleOptions);
    ELG_LAYOUT(ElgBoxCheckboxStyleOptions);
    ELG_LAYOUT(ElgBoxNumberInputStyleOptions);
    ELG_LAYOUT(ElgProgressStyleOptions);
    ELG_LAYOUT(ElgKnobSliderStyleOptions);
    ELG_LAYOUT(ElgBarNavigationStackStyleOptions);
    ELG_LAYOUT(ElgHighlightRowStyleOptions);
    ELG_CATCH_VOID
}
