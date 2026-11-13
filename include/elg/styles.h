// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_STYLES_H
#define ELG_STYLES_H

// Control styles (design.md §8): controls own behavior; a style decides appearance.
//
//   struct PillButtonStyle : elg::ButtonStyle {
//       View make_body(const ButtonConfiguration& button) const override {
//           return button.label
//               .padding(EdgeInsets::symmetric(16, 8))
//               .background([=] { return button.is_pressed() ? accent_dark : accent; }, Capsule());
//       }
//   };
//
//   VStack(Button("Save", save), Button("Cancel", cancel)).button_style(PillButtonStyle{});

#include <elg/animation.h>
#include <elg/export.h>
#include <elg/view.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace elg {

/// What a ButtonStyle gets to build a button's appearance from. The state readers are tracked
/// reads, so use them inside reactive arguments; the body itself runs once.
struct ButtonConfiguration {
    View label;              ///< what the button shows
    Signal<bool> is_pressed; ///< pressed with the pointer still over it
    Signal<bool> is_hovered; ///< the pointer is over it
    Signal<bool> is_enabled; ///< false inside .disabled(true)
    Signal<bool> is_focused; ///< has keyboard focus
};

/// How a Button looks. Subclass it and build the body from the label and the button's state; set
/// it with .button_style() on a view, for every Button inside. ProminentButtonStyle by default.
class ELG_API ButtonStyle {
public:
    virtual ~ButtonStyle();
    /// Called once per button when it's built, while Environment<K>() is available. The button
    /// handles the pointer, keys and accessibility itself; the body only draws.
    ///
    /// @param button The label and the button's state.
    /// @return The button's appearance.
    [[nodiscard]] virtual View make_body(const ButtonConfiguration& button) const = 0;

protected:
    // Styles are copied as their concrete type, never as a ButtonStyle.
    ButtonStyle() = default;
    ButtonStyle(const ButtonStyle&) = default;
    ButtonStyle(ButtonStyle&&) = default;
    ButtonStyle& operator=(const ButtonStyle&) = default;
    ButtonStyle& operator=(ButtonStyle&&) = default;
};

/// Filled with the tint, label in ThemeColor::on_accent. The default.
class ELG_API ProminentButtonStyle final : public ButtonStyle {
public:
    /// The label in semibold, padded, on a rounded rectangle in the tint: lighter while hovered,
    /// darker while pressed, faint while disabled.
    ///
    /// @param button The label and the button's state.
    /// @return The button's appearance.
    [[nodiscard]] View make_body(const ButtonConfiguration& button) const override;
};

/// A raised surface with an outline, label in the tint.
class ELG_API BorderedButtonStyle final : public ButtonStyle {
public:
    /// The label in semibold and the tint (ThemeColor::tertiary while disabled), padded, on a
    /// rounded raised surface with an outline that darkens while hovered and pressed.
    ///
    /// @param button The label and the button's state.
    /// @return The button's appearance.
    [[nodiscard]] View make_body(const ButtonConfiguration& button) const override;
};

/// Just the label, in the colors around it (not the tint), dimmed while pressed or disabled.
class ELG_API PlainButtonStyle final : public ButtonStyle {
public:
    /// The label at 60% opacity while pressed and 40% while disabled.
    ///
    /// @param button The label and the button's state.
    /// @return The button's appearance.
    [[nodiscard]] View make_body(const ButtonConfiguration& button) const override;
};

/// What a ToggleStyle gets to build a toggle's appearance from.
struct ToggleConfiguration {
    View label;                   ///< empty (View::is_empty()) for Toggle(is_on)
    Signal<bool> is_on;           ///< a tracked read
    std::function<void()> toggle; ///< flips it (with the style's animation())
    Signal<bool> is_pressed;      ///< pressed with the pointer still over it
    Signal<bool> is_hovered;      ///< the pointer is over it
    Signal<bool> is_enabled;      ///< false inside .disabled(true)
    Signal<bool> is_focused;      ///< has keyboard focus
};

/// How a Toggle looks. Subclass it and build the body from the label and the toggle's state; set
/// it with .toggle_style() on a view, for every Toggle inside. SwitchToggleStyle by default.
class ELG_API ToggleStyle {
public:
    virtual ~ToggleStyle();
    /// Called once per toggle when it's built, while Environment<K>() is available. The toggle
    /// flips on click, Space and Enter, and handles accessibility itself; the body only draws.
    ///
    /// @param toggle The label and the toggle's state.
    /// @return The toggle's appearance.
    [[nodiscard]] virtual View make_body(const ToggleConfiguration& toggle) const = 0;
    /// How a flip animates; nullopt: at once. Read once per toggle when it's built.
    ///
    /// @return The animation; Animation::smooth(0.3) unless overridden.
    [[nodiscard]] virtual std::optional<Animation> animation() const { return Animation::smooth(0.3); }

protected:
    // Styles are copied as their concrete type, never as a ToggleStyle.
    ToggleStyle() = default;
    ToggleStyle(const ToggleStyle&) = default;
    ToggleStyle(ToggleStyle&&) = default;
    ToggleStyle& operator=(const ToggleStyle&) = default;
    ToggleStyle& operator=(ToggleStyle&&) = default;
};

/// How a SwitchToggleStyle looks; sizes are in points.
struct SwitchToggleStyleOptions {
    std::optional<Prop<Color>> on;         ///< the track while on; nullopt: the tint
    Prop<Color> off = ThemeColor::outline; ///< the track while off
    Prop<Color> knob = Color::white;       ///< the knob's color
    float width = 40;                      ///< the switch's width
    float height = 24;                     ///< the switch's height; the knob is as tall, less the insets
    float knob_inset = 2;                  ///< between the knob and the track's edge
    float spacing = 8;                     ///< between the label and the switch
    /// The label at the leading edge and the switch at the trailing one, across the width it's
    /// offered (a settings row). Otherwise it's as wide as the label and switch.
    bool fills_width = false;
    std::optional<Animation> animation = Animation::smooth(0.3); ///< nullopt: flips at once
};

/// The label, then a sliding switch in the tint. The default.
///
/// @code
/// Toggle("Wi-Fi", wifi).toggle_style(SwitchToggleStyle{{.on = ThemeColor::success, .fills_width = true}})
/// @endcode
class ELG_API SwitchToggleStyle final : public ToggleStyle {
public:
    /// A switch drawn as `options` say.
    ///
    /// @param options The colors, sizes, layout and animation.
    explicit SwitchToggleStyle(SwitchToggleStyleOptions options = {}) : options_(std::move(options)) {}
    /// The label (ThemeColor::tertiary while disabled), then a capsule track whose knob slides to
    /// the trailing end while on. The track is lighter while hovered, darker while pressed, and
    /// faint while disabled. Without a label, just the switch.
    ///
    /// @param toggle The label and the toggle's state.
    /// @return The toggle's appearance.
    [[nodiscard]] View make_body(const ToggleConfiguration& toggle) const override;
    /// How a flip animates.
    ///
    /// @return The options' `animation`.
    [[nodiscard]] std::optional<Animation> animation() const override { return options_.animation; }

private:
    SwitchToggleStyleOptions options_;
};

/// What a CheckboxStyle gets to build a check box's appearance from.
struct CheckboxConfiguration {
    View label;                   ///< empty (View::is_empty()) for Checkbox(is_checked)
    Signal<bool> is_checked;      ///< a tracked read
    Signal<bool> is_mixed;        ///< some of what it stands for is checked (CheckboxOptions::mixed)
    std::function<void()> toggle; ///< checks or unchecks it; a mixed one becomes checked
    Signal<bool> is_pressed;      ///< pressed with the pointer still over it
    Signal<bool> is_hovered;      ///< the pointer is over it
    Signal<bool> is_enabled;      ///< false inside .disabled(true)
    Signal<bool> is_focused;      ///< has keyboard focus
};

/// How a Checkbox looks. Subclass it and build the body from the label and the check box's state;
/// set it with .checkbox_style() on a view, for every Checkbox inside. BoxCheckboxStyle by default.
class ELG_API CheckboxStyle {
public:
    virtual ~CheckboxStyle();
    /// Called once per check box when it's built, while Environment<K>() is available. The check
    /// box toggles on click, Space and Enter, and handles accessibility itself; the body only draws.
    ///
    /// @param checkbox The label and the check box's state.
    /// @return The check box's appearance.
    [[nodiscard]] virtual View make_body(const CheckboxConfiguration& checkbox) const = 0;
    /// How checking and unchecking animate; nullopt: at once. Read once per check box when it's
    /// built.
    ///
    /// @return The animation; Animation::smooth(0.3) unless overridden.
    [[nodiscard]] virtual std::optional<Animation> animation() const { return Animation::smooth(0.3); }

protected:
    // Styles are copied as their concrete type, never as a CheckboxStyle.
    CheckboxStyle() = default;
    CheckboxStyle(const CheckboxStyle&) = default;
    CheckboxStyle(CheckboxStyle&&) = default;
    CheckboxStyle& operator=(const CheckboxStyle&) = default;
    CheckboxStyle& operator=(CheckboxStyle&&) = default;
};

/// How a BoxCheckboxStyle looks; sizes are in points.
struct BoxCheckboxStyleOptions {
    float size = 18;                                ///< the box's width and height
    float corner_radius = 5;                        ///< the box's corners
    std::optional<Prop<Color>> fill;                ///< checked or mixed; nullopt: the tint
    Prop<Color> empty = ThemeColor::surface_raised; ///< unchecked
    Prop<Color> border = ThemeColor::outline;       ///< unchecked; checked or mixed, it's the fill
    float border_width = 1.5f;                      ///< the border's width
    Prop<Color> mark = ThemeColor::on_accent;       ///< the check and the dash
    std::optional<View> check;                      ///< shown while checked; nullopt: a check mark
    std::optional<View> dash;                       ///< shown while mixed; nullopt: a dash
    float spacing = 8;                              ///< between the box and the label
    std::optional<Animation> animation = Animation::smooth(0.3); ///< nullopt: changes at once
};

/// A box, then the label: filled in the tint with a check mark while checked, a dash while mixed.
/// The default.
///
/// @code
/// Checkbox("Agree", agreed).checkbox_style(BoxCheckboxStyle{{.size = 16, .corner_radius = 3}})
/// @endcode
class ELG_API BoxCheckboxStyle final : public CheckboxStyle {
public:
    /// A box drawn as `options` say.
    ///
    /// @param options The sizes, colors, marks, spacing and animation.
    explicit BoxCheckboxStyle(BoxCheckboxStyleOptions options = {}) : options_(std::move(options)) {}
    /// The box (darker while pressed, faint while disabled), then the label (ThemeColor::tertiary
    /// while disabled). Without a label, just the box.
    ///
    /// @param checkbox The label and the check box's state.
    /// @return The check box's appearance.
    [[nodiscard]] View make_body(const CheckboxConfiguration& checkbox) const override;
    /// How checking and unchecking animate.
    ///
    /// @return The options' `animation`.
    [[nodiscard]] std::optional<Animation> animation() const override { return options_.animation; }

private:
    BoxCheckboxStyleOptions options_;
};

/// What a NumberInputStyle gets: the editable number (put it where the text goes), the steps,
/// and the state.
struct NumberInputConfiguration {
    View field;                      ///< the number's text field (in PlainTextFieldStyle)
    std::function<void()> increment; ///< a step up, within the range
    std::function<void()> decrement; ///< a step down, within the range
    Signal<bool> can_increment;      ///< below the maximum
    Signal<bool> can_decrement;      ///< above the minimum
    Signal<bool> shows_buttons;      ///< NumberInputOptions::shows_buttons
    Signal<bool> is_focused;         ///< being edited
    Signal<bool> is_hovered;         ///< the pointer is over it
    Signal<bool> is_enabled;         ///< false inside .disabled(true)
};

/// How a NumberInput looks. Subclass it and put the field and step buttons together; set it with
/// .number_input_style() on a view, for every NumberInput inside. BoxNumberInputStyle by default.
class ELG_API NumberInputStyle {
public:
    virtual ~NumberInputStyle();
    /// Called once per number input when it's built, while Environment<K>() is available. The
    /// number input handles Up/Down, scrubbing and accessibility itself.
    ///
    /// @param input The field, the steps, and the state.
    /// @return The number input's appearance.
    [[nodiscard]] virtual View make_body(const NumberInputConfiguration& input) const = 0;

protected:
    // Styles are copied as their concrete type, never as a NumberInputStyle.
    NumberInputStyle() = default;
    NumberInputStyle(const NumberInputStyle&) = default;
    NumberInputStyle(NumberInputStyle&&) = default;
    NumberInputStyle& operator=(const NumberInputStyle&) = default;
    NumberInputStyle& operator=(NumberInputStyle&&) = default;
};

/// How a BoxNumberInputStyle looks; sizes are in points.
struct BoxNumberInputStyleOptions {
    Prop<Color> background = ThemeColor::surface_raised; ///< the box's fill
    Prop<Color> border = ThemeColor::outline;            ///< the outline while not being edited
    std::optional<Prop<Color>> focused_border;           ///< the outline while editing; nullopt: the tint
    float border_width = 1;                              ///< the outline's width
    float corner_radius = 6;                             ///< the box's corners
    EdgeInsets padding = EdgeInsets::symmetric(8, 5);    ///< around the number and arrows
    float arrow_size = 9;                                ///< the stacked arrows' icons
};

/// A raised box with an outline (the tint while editing), and stacked arrows at its trailing
/// edge. The default.
///
/// @code
/// NumberInput(size, {.min = 8}).number_input_style(BoxNumberInputStyle{{.corner_radius = 4}})
/// @endcode
class ELG_API BoxNumberInputStyle final : public NumberInputStyle {
public:
    /// A box drawn as `options` say.
    ///
    /// @param options The colors, sizes and padding.
    explicit BoxNumberInputStyle(BoxNumberInputStyleOptions options = {}) : options_(std::move(options)) {}
    /// The field and (while shown) the arrows, padded on the box; at half opacity while disabled.
    ///
    /// @param input The field, the steps, and the state.
    /// @return The number input's appearance.
    [[nodiscard]] View make_body(const NumberInputConfiguration& input) const override;

private:
    BoxNumberInputStyleOptions options_;
};

/// Just the number, and the arrows if shown: for putting it in a box of your own.
class ELG_API PlainNumberInputStyle final : public NumberInputStyle {
public:
    /// The field, then (while shown) the arrows stacked at its trailing edge.
    ///
    /// @param input The field, the steps, and the state.
    /// @return The number input's appearance.
    [[nodiscard]] View make_body(const NumberInputConfiguration& input) const override;
};

/// What a ProgressStyle gets: how far along it is.
struct ProgressConfiguration {
    Signal<std::optional<float>> fraction; ///< 0...1; empty: indeterminate (activity of unknown length)
};

/// How a Progress looks. Subclass it and draw the fraction; set it with .progress_style() on a
/// view, for every Progress inside. LinearProgressStyle by default.
class ELG_API ProgressStyle {
public:
    virtual ~ProgressStyle();
    /// Called once per Progress when it's built, while Environment<K>() is available. The
    /// Progress handles accessibility itself.
    ///
    /// @param progress How far along it is.
    /// @return The indicator.
    [[nodiscard]] virtual View make_body(const ProgressConfiguration& progress) const = 0;

protected:
    // Styles are copied as their concrete type, never as a ProgressStyle.
    ProgressStyle() = default;
    ProgressStyle(const ProgressStyle&) = default;
    ProgressStyle(ProgressStyle&&) = default;
    ProgressStyle& operator=(const ProgressStyle&) = default;
    ProgressStyle& operator=(ProgressStyle&&) = default;
};

/// How a LinearProgressStyle looks.
struct LinearProgressStyleOptions {
    Prop<Color> track = ThemeColor::outline; ///< the bar's background
    std::optional<Prop<Color>> fill;         ///< nullopt: the tint
    float thickness = 6;                     ///< the bar's height, in points
};

/// A bar as wide as offered, filled to the fraction, or with a segment sweeping across while
/// indeterminate. The default.
///
/// @code
/// Progress(upload.fraction()).progress_style(LinearProgressStyle{{.thickness = 3}})
/// @endcode
class ELG_API LinearProgressStyle final : public ProgressStyle {
public:
    /// A bar drawn as `options` say.
    ///
    /// @param options The colors and thickness.
    explicit LinearProgressStyle(LinearProgressStyleOptions options = {}) : options_(std::move(options)) {}
    /// The bar: as wide as offered (160 points where the width is left open), `thickness` tall.
    ///
    /// @param progress How far along it is.
    /// @return The bar.
    [[nodiscard]] View make_body(const ProgressConfiguration& progress) const override;

private:
    LinearProgressStyleOptions options_;
};

/// How a CircularProgressStyle looks; sizes are in points.
struct CircularProgressStyleOptions {
    Prop<Color> track = ThemeColor::outline; ///< the ring's background
    std::optional<Prop<Color>> fill;         ///< nullopt: the tint
    float size = 20;                         ///< the ring's diameter
    float line_width = 3;                    ///< the ring's line width
};

/// A ring filled clockwise from the top to the fraction, or with an arc spinning while
/// indeterminate.
///
/// @code
/// Progress().progress_style(CircularProgressStyle{{.size = 14, .line_width = 2}})
/// @endcode
class ELG_API CircularProgressStyle final : public ProgressStyle {
public:
    /// A ring drawn as `options` say.
    ///
    /// @param options The colors and sizes.
    explicit CircularProgressStyle(CircularProgressStyleOptions options = {}) : options_(std::move(options)) {}
    /// The ring, `size` across whatever it's offered.
    ///
    /// @param progress How far along it is.
    /// @return The ring.
    [[nodiscard]] View make_body(const ProgressConfiguration& progress) const override;

private:
    CircularProgressStyleOptions options_;
};

/// What a PickerStyle gets: the options' labels, which one is chosen, and choosing one.
struct PickerConfiguration {
    std::vector<std::string> labels;         ///< the options' labels, in order
    Signal<std::size_t> selected;            ///< the chosen option's index; labels.size() while none is
    std::function<void(std::size_t)> select; ///< chooses the option at an index below labels.size()
};

/// How a Picker looks. Subclass it and build a body that shows the options and calls `select`;
/// set it with .picker_style() on a view, for every Picker inside. SegmentedPickerStyle by
/// default.
class ELG_API PickerStyle {
public:
    virtual ~PickerStyle();
    /// Called once per Picker when it's built, while Environment<K>() is available. The body is
    /// the whole picker: it handles choosing (with buttons or a menu, say) itself.
    ///
    /// @param picker The labels, the chosen one, and choosing one.
    /// @return The picker.
    [[nodiscard]] virtual View make_body(const PickerConfiguration& picker) const = 0;

protected:
    // Styles are copied as their concrete type, never as a PickerStyle.
    PickerStyle() = default;
    PickerStyle(const PickerStyle&) = default;
    PickerStyle(PickerStyle&&) = default;
    PickerStyle& operator=(const PickerStyle&) = default;
    PickerStyle& operator=(PickerStyle&&) = default;
};

/// Every option visible, as segments in a rounded track; the chosen one is raised. The default.
class ELG_API SegmentedPickerStyle final : public PickerStyle {
public:
    /// A Button per option, side by side on a rounded track. Choosing one animates the change.
    ///
    /// @param picker The labels, the chosen one, and choosing one.
    /// @return The picker.
    [[nodiscard]] View make_body(const PickerConfiguration& picker) const override;
};

/// The chosen option on a button; the others in a menu it opens, the chosen one checked.
///
/// @code
/// Picker(sort.binding(), {{Sort::name, "Name"}, {Sort::date, "Date"}}).picker_style(MenuPickerStyle{})
/// @endcode
class ELG_API MenuPickerStyle final : public PickerStyle {
public:
    /// A MenuButton in BorderedButtonStyle showing the chosen option's label, whose menu has a
    /// MenuItem per option.
    ///
    /// @param picker The labels, the chosen one, and choosing one.
    /// @return The picker.
    [[nodiscard]] View make_body(const PickerConfiguration& picker) const override;
};

/// What a SliderStyle gets: where the value is and the slider's state.
struct SliderConfiguration {
    Signal<float> fraction;  ///< where the value is between min and max, 0...1
    Signal<bool> is_pressed; ///< being dragged
    Signal<bool> is_hovered; ///< the pointer is over it
    Signal<bool> is_enabled; ///< false inside .disabled(true)
    Signal<bool> is_focused; ///< has keyboard focus
};

/// How a Slider looks, in three parts that the slider places itself, so a style doesn't need to
/// know its width:
/// - the track, offered the slider's length less the thumb's width (centered, so the thumb stays
///   inside at either end) and taking its own height;
/// - the fill, over the track from its start to the thumb's center (nothing by default);
/// - the thumb, at its ideal size, centered on the value.
/// The slider is as tall as the taller of the track and the thumb. It handles the pointer, keys
/// and accessibility itself; the parts only draw. Set it with .slider_style() on a view, for
/// every Slider inside; KnobSliderStyle by default.
class ELG_API SliderStyle {
public:
    virtual ~SliderStyle();
    /// The track. Called once per Slider when it's built, while Environment<K>() is available,
    /// as are make_fill() and make_thumb().
    ///
    /// @param slider The value's position and the slider's state.
    /// @return The track.
    [[nodiscard]] virtual View make_track(const SliderConfiguration& slider) const = 0;
    /// The fill, from the track's start to the thumb's center.
    ///
    /// @param slider The value's position and the slider's state.
    /// @return The fill; an empty view unless overridden.
    [[nodiscard]] virtual View make_fill(const SliderConfiguration& slider) const;
    /// The thumb, centered on the value.
    ///
    /// @param slider The value's position and the slider's state.
    /// @return The thumb.
    [[nodiscard]] virtual View make_thumb(const SliderConfiguration& slider) const = 0;

protected:
    // Styles are copied as their concrete type, never as a SliderStyle.
    SliderStyle() = default;
    SliderStyle(const SliderStyle&) = default;
    SliderStyle(SliderStyle&&) = default;
    SliderStyle& operator=(const SliderStyle&) = default;
    SliderStyle& operator=(SliderStyle&&) = default;
};

/// How a KnobSliderStyle looks; sizes are in points.
struct KnobSliderStyleOptions {
    Prop<Color> track = ThemeColor::outline; ///< the track's color
    std::optional<Prop<Color>> fill;         ///< nullopt: the tint
    float track_thickness = 4;               ///< the track's and fill's height
    float knob_size = 20;                    ///< the knob's diameter
};

/// A thin capsule track, filled in the tint up to a round white knob with a soft shadow. The
/// default.
///
/// @code
/// Slider(volume.binding()).slider_style(KnobSliderStyle{{.track_thickness = 2, .knob_size = 14}})
/// @endcode
class ELG_API KnobSliderStyle final : public SliderStyle {
public:
    /// A track and knob drawn as `options` say.
    ///
    /// @param options The colors and sizes.
    explicit KnobSliderStyle(KnobSliderStyleOptions options = {}) : options_(std::move(options)) {}
    /// A capsule in the track color.
    ///
    /// @param slider The value's position and the slider's state.
    /// @return The track.
    [[nodiscard]] View make_track(const SliderConfiguration& slider) const override;
    /// A capsule in the fill color, faint while disabled.
    ///
    /// @param slider The value's position and the slider's state.
    /// @return The fill.
    [[nodiscard]] View make_fill(const SliderConfiguration& slider) const override;
    /// A white circle with a thin outline: tinted slightly while hovered, grayer while pressed. It
    /// casts a soft shadow, except while disabled, when it takes ThemeColor::surface_raised.
    ///
    /// @param slider The value's position and the slider's state.
    /// @return The knob.
    [[nodiscard]] View make_thumb(const SliderConfiguration& slider) const override;

private:
    KnobSliderStyleOptions options_;
};

/// What a RowStyle gets for one selectable row (View::selection): the ForEach item's view, and its
/// state. The row itself handles clicks, keys and accessibility.
struct RowConfiguration {
    View content;             ///< the ForEach item's view
    Axis axis{};              ///< the stack's: rows usually span across it
    Signal<bool> is_selected; ///< the row is in the selection
    Signal<bool> is_hovered;  ///< the pointer is over it
    Signal<bool> is_focused;  ///< the row keys act on, while the stack has keyboard focus (Tab)
    Signal<bool> is_enabled;  ///< false inside .disabled(true)
};

/// How the selectable rows of a stack look (View::selection). Subclass it and build a row's body
/// from its content and state; set it with .row_style() on a view, for the selectable rows
/// inside. HighlightRowStyle by default.
class ELG_API RowStyle {
public:
    virtual ~RowStyle();
    /// Called once per row when it's built, while Environment<K>() is available.
    ///
    /// @param row The item's view and the row's state.
    /// @return The row's appearance.
    [[nodiscard]] virtual View make_body(const RowConfiguration& row) const = 0;

protected:
    // Styles are copied as their concrete type, never as a RowStyle.
    RowStyle() = default;
    RowStyle(const RowStyle&) = default;
    RowStyle(RowStyle&&) = default;
    RowStyle& operator=(const RowStyle&) = default;
    RowStyle& operator=(RowStyle&&) = default;
};

/// How a HighlightRowStyle looks.
struct HighlightRowStyleOptions {
    std::optional<Prop<Color>> selected;              ///< the selected rows' highlight; nullopt: the tint
    Prop<Color> on_selected = ThemeColor::on_accent;  ///< their text and icons
    Prop<Color> hovered = ThemeColor::outline;        ///< the hovered row's highlight, at half opacity
    float corner_radius = 6;                          ///< the highlight's corners, in points
    EdgeInsets padding = EdgeInsets::symmetric(8, 4); ///< around the content, inside the highlight
};

/// The content, padded, on a rounded highlight: the tint while selected (with the text and icons
/// in on_accent), faint while hovered, outlined in the tint while focused. The default.
///
/// @code
/// list.row_style(HighlightRowStyle{{.corner_radius = 0, .padding = EdgeInsets::symmetric(16, 10)}})
/// @endcode
class ELG_API HighlightRowStyle final : public RowStyle {
public:
    /// A highlight drawn as `options` say.
    ///
    /// @param options The colors, corners and padding.
    explicit HighlightRowStyle(HighlightRowStyleOptions options = {}) : options_(std::move(options)) {}
    /// The content, padded and spanning the stack's cross axis, on the highlight. Disabled rows
    /// show no hover, and a selected one's highlight at half opacity.
    ///
    /// @param row The item's view and the row's state.
    /// @return The row's appearance.
    [[nodiscard]] View make_body(const RowConfiguration& row) const override;

private:
    HighlightRowStyleOptions options_;
};

/// Just the content, for rows that show their state themselves (Environment<RowSelectedKey>()).
class ELG_API PlainRowStyle final : public RowStyle {
public:
    /// The item's view, unchanged.
    ///
    /// @param row The item's view and the row's state.
    /// @return `row.content`.
    [[nodiscard]] View make_body(const RowConfiguration& row) const override;
};

/// What a TextFieldStyle gets: the editable text (put it where the text goes) and its state.
struct TextFieldConfiguration {
    View field;              ///< the editable text; it takes the font and colors where it's put
    Signal<bool> is_focused; ///< has keyboard focus
    Signal<bool> is_hovered; ///< the pointer is over it
    Signal<bool> is_enabled; ///< false inside .disabled(true)
};

/// How a TextField or TextArea looks. Subclass it and put the field in a box of your own; set it
/// with .text_field_style() on a view, for every TextField and TextArea inside.
/// RoundedBorderTextFieldStyle by default.
class ELG_API TextFieldStyle {
public:
    virtual ~TextFieldStyle();
    /// Called once per text field when it's built, while Environment<K>() is available. The field
    /// handles typing, selection, the pointer and accessibility itself.
    ///
    /// @param field The editable text and its state.
    /// @return The text field's appearance.
    [[nodiscard]] virtual View make_body(const TextFieldConfiguration& field) const = 0;

protected:
    // Styles are copied as their concrete type, never as a TextFieldStyle.
    TextFieldStyle() = default;
    TextFieldStyle(const TextFieldStyle&) = default;
    TextFieldStyle(TextFieldStyle&&) = default;
    TextFieldStyle& operator=(const TextFieldStyle&) = default;
    TextFieldStyle& operator=(TextFieldStyle&&) = default;
};

/// A raised box with an outline that turns the tint while focused. The default.
class ELG_API RoundedBorderTextFieldStyle final : public TextFieldStyle {
public:
    /// The field, padded, on a rounded raised surface (faint while disabled) with an outline that
    /// strengthens while hovered, and a 2-point ring in the tint just outside it while focused.
    ///
    /// @param field The editable text and its state.
    /// @return The text field's appearance.
    [[nodiscard]] View make_body(const TextFieldConfiguration& field) const override;
};

/// Just the text.
class ELG_API PlainTextFieldStyle final : public TextFieldStyle {
public:
    /// The editable text, unchanged.
    ///
    /// @param field The editable text and its state.
    /// @return `field.field`.
    [[nodiscard]] View make_body(const TextFieldConfiguration& field) const override;
};

/// What a PopoverStyle wraps in a panel: the popover's content.
struct PopoverConfiguration {
    View content;           ///< what .popover()'s content function built
    Signal<bool> is_padded; ///< PopoverOptions::padded: false lets the content reach the edges
};

/// How popovers look: the panel around their content. Subclass it, or use PlainPopoverStyle to
/// draw the panel in the content; set it with .popover_style() on a view, for the popovers opened
/// from inside. DefaultPopoverStyle by default.
class ELG_API PopoverStyle {
public:
    virtual ~PopoverStyle();
    /// Called each time it opens, while Environment<K>() is available.
    ///
    /// @param popover The content, and whether to pad it.
    /// @return The panel.
    [[nodiscard]] virtual View make_body(const PopoverConfiguration& popover) const = 0;
    /// How it opens and closes; at once by default. PopoverOptions::motion overrides it.
    ///
    /// @return The motion; PopupMotion::none() unless overridden.
    [[nodiscard]] virtual PopupMotion motion() const { return PopupMotion::none(); }

protected:
    // Styles are copied as their concrete type, never as a PopoverStyle.
    PopoverStyle() = default;
    PopoverStyle(const PopoverStyle&) = default;
    PopoverStyle(PopoverStyle&&) = default;
    PopoverStyle& operator=(const PopoverStyle&) = default;
    PopoverStyle& operator=(PopoverStyle&&) = default;
};

/// A rounded surface with an outline and a shadow; padded content is inset 12 points. The default.
class ELG_API DefaultPopoverStyle final : public PopoverStyle {
public:
    /// The content, inset 12 points if padded and clipped to the panel's rounded corners.
    ///
    /// @param popover The content, and whether to pad it.
    /// @return The panel.
    [[nodiscard]] View make_body(const PopoverConfiguration& popover) const override;
};

/// Just the content: no panel, padding or shadow.
class ELG_API PlainPopoverStyle final : public PopoverStyle {
public:
    /// The content, unchanged (and unpadded).
    ///
    /// @param popover The content, and whether to pad it.
    /// @return `popover.content`.
    [[nodiscard]] View make_body(const PopoverConfiguration& popover) const override;
};

/// What a TooltipStyle wraps: the tooltip's content.
struct TooltipConfiguration {
    View content; ///< .tooltip(text): the text
};

/// How tooltips look: the panel around their content. Set it with .tooltip_style() on a view,
/// for the tooltips inside. DefaultTooltipStyle by default.
class ELG_API TooltipStyle {
public:
    virtual ~TooltipStyle();
    /// Called each time it opens, while Environment<K>() is available.
    ///
    /// @param tooltip The content.
    /// @return The panel.
    [[nodiscard]] virtual View make_body(const TooltipConfiguration& tooltip) const = 0;
    /// How it opens and closes; at once by default. TooltipOptions::motion overrides it.
    ///
    /// @return The motion; PopupMotion::none() unless overridden.
    [[nodiscard]] virtual PopupMotion motion() const { return PopupMotion::none(); }

protected:
    // Styles are copied as their concrete type, never as a TooltipStyle.
    TooltipStyle() = default;
    TooltipStyle(const TooltipStyle&) = default;
    TooltipStyle(TooltipStyle&&) = default;
    TooltipStyle& operator=(const TooltipStyle&) = default;
    TooltipStyle& operator=(TooltipStyle&&) = default;
};

/// Small text on a rounded surface with an outline and a shadow. The default.
class ELG_API DefaultTooltipStyle final : public TooltipStyle {
public:
    /// The content in 12-point ThemeColor::primary type, padded, on the panel.
    ///
    /// @param tooltip The content.
    /// @return The panel.
    [[nodiscard]] View make_body(const TooltipConfiguration& tooltip) const override;
};

/// Just the content.
class ELG_API PlainTooltipStyle final : public TooltipStyle {
public:
    /// The content, unchanged.
    ///
    /// @param tooltip The content.
    /// @return `tooltip.content`.
    [[nodiscard]] View make_body(const TooltipConfiguration& tooltip) const override;
};

/// What a MenuStyle wraps in a panel: a Menu's items.
struct MenuConfiguration {
    View items; ///< the rows, stacked; rows span the panel's width
};

/// What a MenuStyle gets for one row (a MenuItem or a Submenu): its parts, laid out by the style,
/// and its state. The row itself handles clicks, keys and opening submenus.
struct MenuItemConfiguration {
    View title;                  ///< the item's title, as Text
    View leading;                ///< the check mark and icon; empty (View::is_empty()) when the item has neither
    View trailing;               ///< the shortcut hint, or a submenu's chevron; empty when there's neither
    Signal<bool> is_highlighted; ///< hovered, or focused with the keyboard
    Signal<bool> is_enabled;     ///< false inside .disabled(true)
    Signal<bool> is_destructive; ///< MenuItemOptions::destructive
};

/// How menus look: the panel and each row. Subclass it; set it with .menu_style() on a view, for
/// the menus opened from inside. DefaultMenuStyle by default.
class ELG_API MenuStyle {
public:
    virtual ~MenuStyle();
    /// Called each time it opens, while Environment<K>() is available.
    ///
    /// @param menu The rows.
    /// @return The panel.
    [[nodiscard]] virtual View make_body(const MenuConfiguration& menu) const = 0;
    /// A row: by default the parts in a line, padded, in the danger color if destructive, dimmed
    /// when disabled, on a rounded highlight. Called once per row, while Environment<K>() is
    /// available.
    ///
    /// @param item The row's parts and state.
    /// @return The row's appearance.
    [[nodiscard]] virtual View make_item(const MenuItemConfiguration& item) const;
    /// How it opens and closes; at once by default. MenuButtonOptions::motion overrides it for a
    /// MenuButton's menu.
    ///
    /// @return The motion; PopupMotion::none() unless overridden.
    [[nodiscard]] virtual PopupMotion motion() const { return PopupMotion::none(); }

protected:
    // Styles are copied as their concrete type, never as a MenuStyle.
    MenuStyle() = default;
    MenuStyle(const MenuStyle&) = default;
    MenuStyle(MenuStyle&&) = default;
    MenuStyle& operator=(const MenuStyle&) = default;
    MenuStyle& operator=(MenuStyle&&) = default;
};

/// A surface with an outline and a shadow, at least 200 points wide. The default.
class ELG_API DefaultMenuStyle final : public MenuStyle {
public:
    /// The items, padded, on the panel.
    ///
    /// @param menu The rows.
    /// @return The panel.
    [[nodiscard]] View make_body(const MenuConfiguration& menu) const override;
};

/// Just the items.
class ELG_API PlainMenuStyle final : public MenuStyle {
public:
    /// The items, unchanged.
    ///
    /// @param menu The rows.
    /// @return `menu.items`.
    [[nodiscard]] View make_body(const MenuConfiguration& menu) const override;
};

} // namespace elg

#endif // ELG_STYLES_H
