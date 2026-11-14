// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Built-in views and modifiers, text, shapes, paths, drawing, images, menus, navigation.

#include "shim.h"

#include <array>
#include <cstdio>
#include <format>
#include <memory>
#include <print>

using namespace elg;
using namespace elg_shim;

#define VIEW_TRY(...)                 \
    ELG_TRY return out(__VA_ARGS__); \
    ELG_CATCH(nullptr)

namespace {

// Releases, when a C API call returns or throws, what the elements of a consumed array of structs
// hold (closures, handles), from the first element not handed out yet: `release_held` releases
// one element. next() hands out each element in turn, as a copy for the caller's own Consumed to
// guard from then on. Declare it first, like Consumed.
template <class T>
class ConsumedElements {
public:
    ConsumedElements(const T* items, size_t count, void (*release_held)(T&) noexcept) noexcept
        : items_(items), count_(count), release_held_(release_held) {}
    ~ConsumedElements() {
        for (; next_ < count_; ++next_) {
            T item = items_[next_];
            release_held_(item);
        }
    }
    ConsumedElements(const ConsumedElements&) = delete;
    ConsumedElements& operator=(const ConsumedElements&) = delete;
    ConsumedElements(ConsumedElements&&) = delete;
    ConsumedElements& operator=(ConsumedElements&&) = delete;

    T next() noexcept { return items_[next_++]; }

private:
    const T* items_;
    size_t count_;
    void (*release_held_)(T&) noexcept;
    size_t next_ = 0;
};

} // namespace

// --- Containers ---

/// Wraps `VStack()` (`VStackLayout`): the children in a column.
///
/// @param alignment How the children line up across the column (`HorizontalAlignment`).
/// @param spacing Between the children; unset: `default_stack_spacing`.
/// @param children The children (consumed).
/// @param count How many `children` there are.
/// @return The stack, owned by the caller, or null on failure.
ELG_EXPORT View* elg_vstack(int32_t alignment, ElgOptLen spacing, View** children, size_t count) {
    ELG_TRY
    ViewArray views{children, count};
    const Consumed consumed(views);
    return out(VStackLayout(static_cast<HorizontalAlignment>(alignment), length(spacing))(take_all(views)));
    ELG_CATCH(nullptr)
}

/// Wraps `HStack()` (`HStackLayout`): the children in a row.
///
/// @param alignment How the children line up across the row (`VerticalAlignment`).
/// @param spacing Between the children; unset: `default_stack_spacing`.
/// @param children The children (consumed).
/// @param count How many `children` there are.
/// @return The stack, owned by the caller, or null on failure.
ELG_EXPORT View* elg_hstack(int32_t alignment, ElgOptLen spacing, View** children, size_t count) {
    ELG_TRY
    ViewArray views{children, count};
    const Consumed consumed(views);
    return out(HStackLayout(static_cast<VerticalAlignment>(alignment), length(spacing))(take_all(views)));
    ELG_CATCH(nullptr)
}

/// Wraps `ZStack()` (`ZStackLayout`): the children drawn on top of each other, the first at the back.
///
/// @param alignment Where children smaller than the stack sit.
/// @param children The children (consumed).
/// @param count How many `children` there are.
/// @return The stack, owned by the caller, or null on failure.
ELG_EXPORT View* elg_zstack(ElgAlignment alignment, View** children, size_t count) {
    ELG_TRY
    ViewArray views{children, count};
    const Consumed consumed(views);
    return out(ZStackLayout(elg_shim::alignment(alignment))(take_all(views)));
    ELG_CATCH(nullptr)
}

/// Wraps `Spacer()`: flexible space that expands along its stack's axis.
///
/// @param min_length The least it shrinks to, in points; unset: `default_stack_spacing`.
/// @return The spacer, owned by the caller, or null on failure.
ELG_EXPORT View* elg_spacer(ElgOptLen min_length) {
    VIEW_TRY(Spacer(length(min_length)))
}

/// Wraps `EmptyView()`: takes no space and draws nothing.
///
/// @return The view, owned by the caller, or null on failure.
ELG_EXPORT View* elg_empty_view() {
    VIEW_TRY(EmptyView())
}

/// Wraps `aligned_position()`: where a child of size `content` goes inside `container`.
///
/// @param container The space the child goes in.
/// @param content The child's size.
/// @param a How the child lines up in `container`.
/// @return The child's top-leading corner, in `container`'s coordinates. Zero on failure.
ELG_EXPORT ElgPoint elg_aligned_position(ElgRect container, ElgSize content, ElgAlignment a) {
    ELG_TRY
    const Point p = aligned_position(rect(container), size(content), alignment(a));
    return {p.x, p.y};
    ELG_CATCH({})
}

/// Wraps `size_that_fits()` of `VStackLayout`, `HStackLayout` or `ZStackLayout`, for custom layouts that
/// measure their children as a stack does.
///
/// @param kind 0: `VStackLayout`; 1: `HStackLayout`; anything else: `ZStackLayout`.
/// @param a The stack's alignment: a VStack reads `horizontal`, an HStack `vertical`, a ZStack both.
/// @param spacing Between the children of a VStack or HStack; unset: `default_stack_spacing`.
/// @param proposal The size offered to the stack.
/// @param children The custom layout's children (borrowed).
/// @param count How many `children` there are.
/// @return The size the stack takes; zero on failure.
ELG_EXPORT ElgSize elg_stack_layout_size(int32_t kind, ElgAlignment a, ElgOptLen spacing, ElgProposedSize proposal,
                                         const LayoutChild* children, size_t count) {
    ELG_TRY
    const std::span<const LayoutChild> span(children, count);
    Size result;
    if (kind == 0) {
        result = VStackLayout(static_cast<HorizontalAlignment>(a.horizontal), length(spacing)).size_that_fits(proposed(proposal), span);
    } else if (kind == 1) {
        result = HStackLayout(static_cast<VerticalAlignment>(a.vertical), length(spacing)).size_that_fits(proposed(proposal), span);
    } else {
        result = ZStackLayout(alignment(a)).size_that_fits(proposed(proposal), span);
    }
    return {result.width, result.height};
    ELG_CATCH(ElgSize{})
}

/// Wraps `place_children()` of `VStackLayout`, `HStackLayout` or `ZStackLayout`, for custom layouts that
/// place their children as a stack does.
///
/// @param kind 0: `VStackLayout`; 1: `HStackLayout`; anything else: `ZStackLayout`.
/// @param a The stack's alignment: a VStack reads `horizontal`, an HStack `vertical`, a ZStack both.
/// @param spacing Between the children of a VStack or HStack; unset: `default_stack_spacing`.
/// @param bounds Where the stack is, at the size elg_stack_layout_size() reported for `proposal`.
/// @param proposal The size offered to the stack.
/// @param children The custom layout's children (borrowed).
/// @param count How many `children` there are.
ELG_EXPORT void elg_stack_layout_place(int32_t kind, ElgAlignment a, ElgOptLen spacing, ElgRect bounds,
                                       ElgProposedSize proposal, const LayoutChild* children, size_t count) {
    ELG_TRY
    const std::span<const LayoutChild> span(children, count);
    if (kind == 0) {
        VStackLayout(static_cast<HorizontalAlignment>(a.horizontal), length(spacing)).place_children(rect(bounds), proposed(proposal), span);
    } else if (kind == 1) {
        HStackLayout(static_cast<VerticalAlignment>(a.vertical), length(spacing)).place_children(rect(bounds), proposed(proposal), span);
    } else {
        ZStackLayout(alignment(a)).place_children(rect(bounds), proposed(proposal), span);
    }
    ELG_CATCH_VOID
}

// --- Controls ---

/// Wraps `Button()` with a view label: a push button. Its look comes from the ButtonStyle.
///
/// @param take_label What the button shows (consumed).
/// @param on_click Runs when the button is clicked, or activated with Space or Enter while it has focus. No
///   args. Required: none is a usage error.
/// @return The button, owned by the caller, or null on failure.
ELG_EXPORT View* elg_button(View* take_label, ElgClosure on_click) {
    ELG_TRY
    const Consumed consumed(take_label, on_click);
    return out(Button(take(take_label), action(required(on_click, "on_click"))));
    ELG_CATCH(nullptr)
}

/// Wraps `Button()` with a title: like elg_button(), labeled with text.
///
/// @param title A string prop: the button's text.
/// @param on_click Runs when the button is clicked, or activated with Space or Enter while it has focus. No
///   args. Required: none is a usage error.
/// @return The button, owned by the caller, or null on failure.
ELG_EXPORT View* elg_button_title(ElgProp title, ElgClosure on_click) {
    ELG_TRY
    const Consumed consumed(title, on_click);
    return out(Button(prop<std::string>(title), action(required(on_click, "on_click"))));
    ELG_CATCH(nullptr)
}

/// The options of elg_text_field() (`TextFieldOptions`) and elg_text_area() (`TextAreaOptions`, which has no
/// `secure` or `line_limit`).
struct ElgTextFieldOptions {
    /// Runs on Enter; in a text area, on Ctrl+Enter (Cmd+Enter on macOS). No args; may be none.
    ElgClosure on_submit;
    /// A bool prop: a password field (`TextFieldOptions::secure`). elg_text_area() doesn't read it (and
    /// drops its closure).
    ElgProp secure;
    /// An int prop: more than 1 wraps the text and grows up to this many lines (`TextFieldOptions::line_limit`).
    /// elg_text_area() doesn't read it (and drops its closure).
    ElgProp line_limit;
    /// Styles ranges of the text after every change (`TextFieldOptions::styler`); may be none.
    /// args: ElgStr* text, ret: std::vector<TextSpan>* (filled with elg_text_spans_push).
    ElgClosure styler;
    /// Sees key presses before the field does (`TextFieldOptions::on_key`); may be none.
    /// args: ElgKeyEvent*, ret: bool* (true takes the key, and the field ignores it).
    ElgClosure on_key;
};

static std::function<std::vector<TextSpan>(std::string_view)> text_styler(SharedFn f) {
    if (!f) {
        return {};
    }
    return [f = std::move(f)](std::string_view text) {
        std::vector<TextSpan> spans;
        ElgStr arg{text.data(), text.size()};
        f->call(&arg, &spans);
        return spans;
    };
}

static std::function<bool(const KeyEvent&)> key_handler(SharedFn f) {
    if (!f) {
        return {};
    }
    return [f = std::move(f)](const KeyEvent& event) {
        ElgKeyEvent arg = c_key_event(event);
        bool handled = false;
        f->call(&arg, &handled);
        return handled;
    };
}

/// Appends a `TextSpan` to a styler's result (ElgTextFieldOptions::styler).
///
/// @param spans The styler's `ret`.
/// @param begin Where the span starts, in bytes into the text.
/// @param end Where it ends, in bytes into the text.
/// @param style How the range looks (borrowed).
/// @param label Null: none. Otherwise the range shows as this text and is edited as one unit, e.g. a mention
///   shown as a chip (copied).
ELG_EXPORT void elg_text_spans_push(std::vector<TextSpan>* spans, size_t begin, size_t end, const ElgRunStyle* style,
                                    const ElgStr* label) {
    ELG_TRY
    TextSpan span;
    span.begin = begin;
    span.end = end;
    span.style = run_style(*style);
    if (label) {
        span.label = str(*label);
    }
    spans->push_back(std::move(span));
    ELG_CATCH_VOID
}

/// Wraps `TextField()`: a single-line text editor bound to `take_text`. Its look comes from the TextFieldStyle.
///
/// @param placeholder A string prop: shown while the text is empty.
/// @param take_text The text (consumed).
/// @param o The options (borrowed; their closures are taken over).
/// @return The text field, owned by the caller, or null on failure: a UsageError when a static `secure` is
///   true and a static `line_limit` is above 1.
ELG_EXPORT View* elg_text_field(ElgProp placeholder, Binding<std::string>* take_text, const ElgTextFieldOptions* o) {
    ELG_TRY
    ElgTextFieldOptions c = *o;
    const Consumed consumed(placeholder, take_text, c.on_submit, c.secure, c.line_limit, c.styler, c.on_key);
    const std::unique_ptr<Binding<std::string>> text = own(take_text);
    TextFieldOptions options;
    options.on_submit = action(share(c.on_submit));
    options.secure = prop<bool>(c.secure);
    options.line_limit = prop<int>(c.line_limit);
    options.styler = text_styler(share(c.styler));
    options.on_key = key_handler(share(c.on_key));
    return out(TextField(prop<std::string>(placeholder), *text, std::move(options)));
    ELG_CATCH(nullptr)
}

/// Wraps `TextArea()`: a multiline text editor bound to `take_text`. It takes the height it's offered and
/// scrolls through longer text; its look comes from the TextFieldStyle.
///
/// @param placeholder A string prop: shown while the text is empty.
/// @param take_text The text (consumed).
/// @param o The options (borrowed; their closures are taken over). `secure` and `line_limit` are
///   ignored, and their closures dropped.
/// @return The text area, owned by the caller, or null on failure.
ELG_EXPORT View* elg_text_area(ElgProp placeholder, Binding<std::string>* take_text, const ElgTextFieldOptions* o) {
    ELG_TRY
    ElgTextFieldOptions c = *o;
    const Consumed consumed(placeholder, take_text, c.on_submit, c.secure, c.line_limit, c.styler, c.on_key);
    const std::unique_ptr<Binding<std::string>> text = own(take_text);
    TextAreaOptions options;
    options.on_submit = action(share(c.on_submit));
    options.styler = text_styler(share(c.styler));
    options.on_key = key_handler(share(c.on_key));
    return out(TextArea(prop<std::string>(placeholder), *text, std::move(options)));
    ELG_CATCH(nullptr)
}

/// Wraps `Slider()`: a value in [`min`, `max`] picked by dragging a knob along a track, or with the arrow
/// keys, Page Up/Down and Home/End while focused. Takes the width it's offered.
///
/// @param take_value The value (consumed).
/// @param min A float prop: the low end.
/// @param max A float prop: the high end.
/// @param step A float prop: the increment; 0: continuous.
/// @param on_editing_changed Called with true when a drag starts and false when it ends. args: bool*; may be
///   none.
/// @return The slider, owned by the caller, or null on failure.
ELG_EXPORT View* elg_slider(Binding<float>* take_value, ElgProp min, ElgProp max, ElgProp step, ElgClosure on_editing_changed) {
    ELG_TRY
    const Consumed consumed(take_value, min, max, step, on_editing_changed);
    const std::unique_ptr<Binding<float>> value = own(take_value);
    SliderOptions options;
    options.min = prop<float>(min);
    options.max = prop<float>(max);
    options.step = prop<float>(step);
    options.on_editing_changed = consumer<bool>(share(on_editing_changed));
    return out(Slider(*value, std::move(options)));
    ELG_CATCH(nullptr)
}

/// The options of elg_number_input(): `NumberInputOptions`.
struct ElgNumberInputOptions {
    /// Double props: the range, and the step of the arrows, Up/Down (Shift: 10 steps) and scrubbing.
    ElgProp min, max, step;
    /// An int prop: digits after the point; -1: as many as the value needs (up to 6).
    ElgProp decimals;
    /// Text for a value; none: a plain number. args: double*, ret: std::string* (write it with
    /// elg_string_assign).
    ElgClosure format;
    /// A value for text; none: plain numbers, ignoring spaces around them. args: ElgStr*, ret: ElgOptNumber*.
    ElgClosure parse;
    /// A bool prop: apply each valid number as it's typed, instead of on Enter or when the field loses focus.
    ElgProp live;
    ElgProp shows_buttons; ///< A bool prop: the style's increment and decrement buttons.
    /// A bool prop: dragging sideways on the field (while it isn't being edited) changes the value.
    ElgProp scrubs;
};

/// What ElgNumberInputOptions::parse writes: a number, or none.
struct ElgOptNumber {
    bool has;     ///< false: the text isn't a number.
    double value; ///< The number, when `has`.
};

/// Wraps `NumberInput()`: a number you can type, step with the arrows or Up/Down, or scrub by dragging. Values
/// stay within [min, max]; text that isn't a number reverts. Its look comes from the NumberInputStyle.
///
/// @param take_value The value (consumed).
/// @param integer Whole numbers: values are rounded and shown without decimals, as `NumberInput()` does for
///   an int.
/// @param o The options (borrowed; their closures are taken over).
/// @return The number input, owned by the caller, or null on failure.
ELG_EXPORT View* elg_number_input(Binding<double>* take_value, bool integer, const ElgNumberInputOptions* o) {
    ELG_TRY
    ElgNumberInputOptions c = *o;
    const Consumed consumed(take_value, c.min, c.max, c.step, c.decimals, c.format, c.parse, c.live, c.shows_buttons, c.scrubs);
    const std::unique_ptr<Binding<double>> value = own(take_value);
    NumberInputOptions options;
    options.min = prop<double>(c.min);
    options.max = prop<double>(c.max);
    options.step = prop<double>(c.step);
    options.decimals = prop<int>(c.decimals);
    if (const SharedFn f = share(c.format)) {
        options.format = [f](double v) {
            std::string result;
            f->call(&v, &result);
            return result;
        };
    }
    if (const SharedFn f = share(c.parse)) {
        options.parse = [f](std::string_view text) {
            ElgStr arg{text.data(), text.size()};
            ElgOptNumber result{};
            f->call(&arg, &result);
            return result.has ? std::optional<double>(result.value) : std::nullopt;
        };
    }
    options.live = prop<bool>(c.live);
    options.shows_buttons = prop<bool>(c.shows_buttons);
    options.scrubs = prop<bool>(c.scrubs);
    return out(detail::make_number_input(*value, integer, std::move(options)));
    ELG_CATCH(nullptr)
}

/// Wraps `Progress()`: how far along something is, drawn by the ProgressStyle.
///
/// @param fraction An optional float prop (ElgOptLen): 0...1, or unset while indeterminate.
/// @return The progress view, owned by the caller, or null on failure.
ELG_EXPORT View* elg_progress(ElgProp fraction) {
    ELG_TRY
    const Consumed consumed(fraction);
    return out(Progress(prop<std::optional<float>>(fraction)));
    ELG_CATCH(nullptr)
}

/// Wraps `Picker()`: one of several options, chosen by index. Its look comes from the PickerStyle.
///
/// @param labels The options' labels (copied).
/// @param count How many `labels` there are.
/// @param selected The chosen option's index, `count` while none is. ret: size_t*. Required: none is a usage
///   error.
/// @param select Chooses an option. args: size_t* (its index). Required: none is a usage error.
/// @return The picker, owned by the caller, or null on failure.
ELG_EXPORT View* elg_picker(const ElgStr* labels, size_t count, ElgClosure selected, ElgClosure select) {
    ELG_TRY
    const Consumed consumed(selected, select);
    std::vector<std::string> names;
    names.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        names.push_back(str(labels[i]));
    }
    return out(detail::make_picker(std::move(names), producer<std::size_t>(required(selected, "selected")),
                                   consumer<std::size_t>(required(select, "select"))));
    ELG_CATCH(nullptr)
}

/// Wraps `Toggle()`: an on/off control bound to `take_is_on`. Its look comes from the ToggleStyle (a switch by
/// default); a click, Space or Enter flips it.
///
/// @param take_label The label (consumed); null: just the switch (give it an accessibility label).
/// @param take_is_on Whether it's on (consumed).
/// @return The toggle, owned by the caller, or null on failure.
ELG_EXPORT View* elg_toggle(View* take_label, Binding<bool>* take_is_on) {
    ELG_TRY
    const Consumed consumed(take_label, take_is_on);
    const std::unique_ptr<Binding<bool>> is_on = own(take_is_on);
    if (!take_label) {
        return out(Toggle(*is_on));
    }
    return out(Toggle(take(take_label), *is_on));
    ELG_CATCH(nullptr)
}

/// Wraps `Toggle()` with a title: like elg_toggle(), labeled with text.
///
/// @param title A string prop: the label.
/// @param take_is_on Whether it's on (consumed).
/// @return The toggle, owned by the caller, or null on failure.
ELG_EXPORT View* elg_toggle_title(ElgProp title, Binding<bool>* take_is_on) {
    ELG_TRY
    const Consumed consumed(title, take_is_on);
    const std::unique_ptr<Binding<bool>> is_on = own(take_is_on);
    return out(Toggle(prop<std::string>(title), *is_on));
    ELG_CATCH(nullptr)
}

/// Wraps `Checkbox()`: a check box bound to `take_is_checked`. Its look comes from the CheckboxStyle; a click,
/// Space or Enter checks or unchecks it.
///
/// @param take_label The label (consumed); null: just the box (give it an accessibility label).
/// @param take_is_checked Whether it's checked (consumed).
/// @param mixed A bool prop: some of what it stands for is checked, shown with a dash (`CheckboxOptions::mixed`).
/// @return The check box, owned by the caller, or null on failure.
ELG_EXPORT View* elg_checkbox(View* take_label, Binding<bool>* take_is_checked, ElgProp mixed) {
    ELG_TRY
    const Consumed consumed(take_label, take_is_checked, mixed);
    const std::unique_ptr<Binding<bool>> is_checked = own(take_is_checked);
    CheckboxOptions options{prop<bool>(mixed)};
    if (!take_label) {
        return out(Checkbox(*is_checked, std::move(options)));
    }
    return out(Checkbox(take(take_label), *is_checked, std::move(options)));
    ELG_CATCH(nullptr)
}

/// Wraps `Checkbox()` with a title: like elg_checkbox(), labeled with text.
///
/// @param title A string prop: the label.
/// @param take_is_checked Whether it's checked (consumed).
/// @param mixed A bool prop: some of what it stands for is checked, shown with a dash (`CheckboxOptions::mixed`).
/// @return The check box, owned by the caller, or null on failure.
ELG_EXPORT View* elg_checkbox_title(ElgProp title, Binding<bool>* take_is_checked, ElgProp mixed) {
    ELG_TRY
    const Consumed consumed(title, take_is_checked, mixed);
    const std::unique_ptr<Binding<bool>> is_checked = own(take_is_checked);
    return out(Checkbox(prop<std::string>(title), *is_checked, CheckboxOptions{prop<bool>(mixed)}));
    ELG_CATCH(nullptr)
}

/// Wraps `Separator()`: a thin line between groups, as long as it's offered.
///
/// @param axis `Axis`: 0 horizontal, 1 vertical.
/// @param color A Color prop: the line's color.
/// @param thickness The line's thickness, in points.
/// @param inset Left clear at both ends, in points.
/// @return The separator, owned by the caller, or null on failure.
ELG_EXPORT View* elg_separator(int32_t axis, ElgProp color, float thickness, float inset) {
    ELG_TRY
    const Consumed consumed(color);
    return out(Separator(static_cast<Axis>(axis), {prop<Color>(color), thickness, inset}));
    ELG_CATCH(nullptr)
}

// --- Structure ---

/// Wraps `If()`: `take_then` while `condition` holds, otherwise `take_else`. Switching branches removes the old
/// branch's views, with any state created inside them, and builds the other.
///
/// @param condition A bool prop.
/// @param take_then Shown while `condition` holds (consumed).
/// @param take_else Shown otherwise (consumed); an empty view for nothing.
/// @return The view, owned by the caller, or null on failure.
ELG_EXPORT View* elg_if(ElgProp condition, View* take_then, View* take_else) {
    ELG_TRY
    const Consumed consumed(condition, take_then, take_else);
    return out(If(prop<bool>(condition), take(take_then), take(take_else)));
    ELG_CATCH(nullptr)
}

/// One ForEach item (`detail::ForEachItem`), for elg_for_each_items_push().
struct ElgForEachItem {
    bool key_is_string; ///< The item's key is `string_key`; otherwise `int_key`.
    int64_t int_key;    ///< The key, when it isn't a string.
    ElgStr string_key;  ///< The key, when it's a string (copied).
    /// Owns the Rust item, which Elegance keeps to compare with the next update's; its `call` is unused
    /// and may be null.
    ElgClosure value;
    /// Compares two items' Rust data (their `value` closures' `data`): true when they're equal, so the item
    /// keeps its view.
    bool (*equals)(void* a, void* b);
    ElgClosure view; ///< Builds the item's view. ret: View*. Required: none is a usage error.
};

namespace {

struct RustItem {
    SharedFn value;
    bool (*equals)(void*, void*);
};

bool rust_items_equal(const void* a, const void* b) {
    const auto* x = static_cast<const RustItem*>(a);
    const auto* y = static_cast<const RustItem*>(b);
    return x->equals == y->equals && x->equals(x->value->data(), y->value->data());
}

void release_held(ElgForEachItem& item) noexcept {
    release(item.value);
    release(item.view);
}

} // namespace

/// Wraps `ForEach()`: one view per item, identified by key. When the items change, unchanged ones keep their
/// views (and state), changed ones are rebuilt and removed ones are removed.
///
/// @param items Lists the items; a tracked read, so the ForEach updates when what it reads changes. ret:
///   std::vector<ForEachItem>* (filled with elg_for_each_items_push). Required: none is a usage error.
/// @return The ForEach, owned by the caller, or null on failure.
ELG_EXPORT View* elg_for_each(ElgClosure items) {
    ELG_TRY
    const Consumed consumed(items);
    const SharedFn f = required(items, "items");
    return out(detail::make_for_each([f] {
        std::vector<detail::ForEachItem> result;
        f->call(nullptr, &result);
        return result;
    }));
    ELG_CATCH(nullptr)
}

/// Appends items to a list of ForEach items: the `ret` of elg_for_each()'s `items` and
/// elg_navigation_stack()'s `pages`.
///
/// @param target The closure's `ret`.
/// @param items The items (borrowed; their `value` and `view` closures are taken over).
/// @param count How many `items` there are.
ELG_EXPORT void elg_for_each_items_push(std::vector<detail::ForEachItem>* target, const ElgForEachItem* items, size_t count) {
    ELG_TRY
    ConsumedElements<ElgForEachItem> elements(items, count, &release_held);
    std::vector<detail::ForEachItem> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        ElgForEachItem item = elements.next();
        const Consumed consumed(item.value, item.view);
        ItemKey key = item.key_is_string ? ItemKey(str(item.string_key)) : ItemKey(item.int_key);
        auto value = std::make_shared<const RustItem>(RustItem{hold(item.value), item.equals});
        result.push_back({std::move(key), value, &rust_items_equal, view_builder(required(item.view, "items[].view"))});
    }
    for (auto& item : result) {
        target->push_back(std::move(item));
    }
    ELG_CATCH_VOID
}

// --- Scrolling ---

/// Wraps `ScrollPosition()`: scrolls a scroll view from code. Pass it to elg_scroll_view(), then call the
/// elg_scroll_position_to_* functions, e.g. from event handlers.
///
/// @return The position, owned by the caller (free it with elg_scroll_position_free()), or null on failure.
ELG_EXPORT ScrollPosition* elg_scroll_position_new() {
    ELG_TRY
    return new ScrollPosition();
    ELG_CATCH(nullptr)
}
/// Copies a ScrollPosition: copies refer to the same scroll view.
///
/// @param position The position to copy (borrowed).
/// @return The copy, owned by the caller. Null on failure.
ELG_EXPORT ScrollPosition* elg_scroll_position_clone(const ScrollPosition* position) {
    ELG_TRY
    return new ScrollPosition(*position);
    ELG_CATCH(nullptr)
}
/// Frees a ScrollPosition. The scroll view keeps its own copy.
///
/// @param position The position (consumed); may be null.
ELG_EXPORT void elg_scroll_position_free(ScrollPosition* position) {
    ELG_TRY
    delete position;
    ELG_CATCH_VOID
}
/// Wraps `ScrollPosition::scroll_to_top()`: scrolls the scroll view to its top.
///
/// @param position The scroll view's position (borrowed).
/// @param animated Whether the scroll animates.
ELG_EXPORT void elg_scroll_position_to_top(const ScrollPosition* position, bool animated) {
    ELG_TRY
    position->scroll_to_top(animated);
    ELG_CATCH_VOID
}
/// Wraps `ScrollPosition::scroll_to_end()`: scrolls the scroll view to its end.
///
/// @param position The scroll view's position (borrowed).
/// @param animated Whether the scroll animates.
ELG_EXPORT void elg_scroll_position_to_end(const ScrollPosition* position, bool animated) {
    ELG_TRY
    position->scroll_to_end(animated);
    ELG_CATCH_VOID
}
/// Wraps `ScrollPosition::scroll_to()` with an integer key: scrolls the item with this ForEach key, in a lazy
/// stack inside the scroll view, to the top.
///
/// @param position The scroll view's position (borrowed).
/// @param key The item's ForEach key.
ELG_EXPORT void elg_scroll_position_to_int(const ScrollPosition* position, int64_t key) {
    ELG_TRY
    position->scroll_to(key);
    ELG_CATCH_VOID
}
/// Wraps `ScrollPosition::scroll_to()` with a string key: scrolls the item with this ForEach key, in a lazy
/// stack inside the scroll view, to the top.
///
/// @param position The scroll view's position (borrowed).
/// @param key The item's ForEach key (copied).
ELG_EXPORT void elg_scroll_position_to_string(const ScrollPosition* position, ElgStr key) {
    ELG_TRY
    position->scroll_to(str(key));
    ELG_CATCH_VOID
}

/// Wraps `ScrollView()`: `take_content` at its ideal size along the scroll axes, clipped to the space the
/// scroll view takes (all it's offered). The wheel, trackpad, scroll bars and keys scroll it.
///
/// @param take_content What scrolls (consumed).
/// @param axes `ScrollAxes`: 1 vertical, 2 horizontal, 3 both.
/// @param shows_indicators A bool prop: scroll bars, shown while scrolling or hovered.
/// @param position Scrolls it from code; null: none. Borrowed: the scroll view keeps a copy, which refers to
///   the same scroll view.
/// @param sticks_to_end A bool prop: starts at the end and, while scrolled to the end, stays there as the
///   content grows (chat logs).
/// @return The scroll view, owned by the caller, or null on failure.
ELG_EXPORT View* elg_scroll_view(View* take_content, int32_t axes, ElgProp shows_indicators, const ScrollPosition* position,
                                 ElgProp sticks_to_end) {
    ELG_TRY
    const Consumed consumed(take_content, shows_indicators, sticks_to_end);
    ScrollViewOptions options;
    options.axes = static_cast<ScrollAxes>(axes);
    options.shows_indicators = prop<bool>(shows_indicators);
    if (position) {
        options.position = *position;
    }
    options.sticks_to_end = prop<bool>(sticks_to_end);
    return out(ScrollView(options, take(take_content)));
    ELG_CATCH(nullptr)
}

/// Lines between a lazy stack's items (`StackSeparators`), for elg_lazy_stack().
struct ElgStackSeparators {
    ElgProp color;   ///< A Color prop: the lines' color.
    float inset;     ///< From the leading edge (the top, in a horizontal stack), in points.
    float thickness; ///< The lines' thickness, in points.
};

// `color`: the caller's copy of `s->color`, which its Consumed guards.
static std::optional<StackSeparators> stack_separators(const ElgStackSeparators* s, ElgProp& color) {
    if (!s) {
        return std::nullopt;
    }
    // libc++'s std::function points into its own small buffer, and re-points it when moved.
    // NOLINTNEXTLINE(clang-analyzer-core.StackAddressEscape)
    return StackSeparators{prop<Color>(color), s->inset, s->thickness};
}

/// Wraps `LazyVStack()` and `LazyHStack()`: like a stack over a ForEach, but only the items in or near the
/// visible part of the enclosing scroll view are built. Outside a scroll view every item is built.
///
/// @param vertical true: a LazyVStack; false: a LazyHStack.
/// @param alignment How the items line up across the stack: a `HorizontalAlignment` in a vertical stack, a
///   `VerticalAlignment` in a horizontal one.
/// @param spacing Between the items; unset: `default_stack_spacing`.
/// @param separators Lines between the items (borrowed; the color's closure is taken over); null: none.
/// @param take_for_each A ForEach from elg_for_each() (consumed).
/// @return The stack, owned by the caller, or null on failure: a UsageError when `take_for_each` isn't a
///   ForEach.
ELG_EXPORT View* elg_lazy_stack(bool vertical, int32_t alignment, ElgOptLen spacing, const ElgStackSeparators* separators,
                                View* take_for_each) {
    ELG_TRY
    ElgProp color = separators ? separators->color : ElgProp{};
    const Consumed consumed(color, take_for_each);
    if (vertical) {
        return out(LazyVStack({static_cast<HorizontalAlignment>(alignment), length(spacing), stack_separators(separators, color)},
                              take(take_for_each)));
    }
    return out(LazyHStack({static_cast<VerticalAlignment>(alignment), length(spacing), stack_separators(separators, color)},
                          take(take_for_each)));
    ELG_CATCH(nullptr)
}

// --- Menus ---

/// Wraps `MenuItem()`: a menu command. Choosing it closes the menu, then runs `on_choose`.
///
/// @param title A string prop: the item's text.
/// @param on_choose Runs when the item is chosen. No args.
/// @param shortcut A string prop: a hint shown on the right, e.g. "Ctrl+R"; empty: none.
/// @param destructive A bool prop: shown in the danger color.
/// @param checked A bool prop for a check mark in front (borrowed; its closure is taken over); null: none.
/// @param image An icon in front (borrowed); null: none.
/// @return The menu item, owned by the caller, or null on failure.
ELG_EXPORT View* elg_menu_item(ElgProp title, ElgClosure on_choose, ElgProp shortcut, ElgProp destructive,
                               const ElgProp* checked, const ElgImageSource* image) {
    ELG_TRY
    ElgProp check = checked ? *checked : ElgProp{};
    const Consumed consumed(title, on_choose, shortcut, destructive, check);
    MenuItemOptions options;
    options.shortcut = prop<std::string>(shortcut);
    options.destructive = prop<bool>(destructive);
    if (checked) {
        options.checked = prop<bool>(check);
    }
    if (image) {
        options.image = image_source(*image);
    }
    return out(MenuItem(prop<std::string>(title), action(share(on_choose)), std::move(options)));
    ELG_CATCH(nullptr)
}

/// Wraps `Submenu()`: a menu item that opens another menu beside it (on hover, click, or Right).
///
/// @param title A string prop: the item's text.
/// @param menu Builds the menu, usually with elg_menu(). ret: View*. Required: none is a usage error.
/// @return The submenu item, owned by the caller, or null on failure.
ELG_EXPORT View* elg_submenu(ElgProp title, ElgClosure menu) {
    ELG_TRY
    const Consumed consumed(title, menu);
    return out(Submenu(prop<std::string>(title), view_builder(required(menu, "menu"))));
    ELG_CATCH(nullptr)
}

/// Wraps `Menu()`: the menu panel, its items stacked in the MenuStyle's panel. Up/Down move between items,
/// Enter chooses, Esc closes, Left/Right leave and enter submenus.
///
/// @param items The items (consumed): elg_menu_item(), elg_submenu(), separators, or any view.
/// @param count How many `items` there are.
/// @return The menu, owned by the caller, or null on failure.
ELG_EXPORT View* elg_menu(View** items, size_t count) {
    ELG_TRY
    ViewArray views{items, count};
    const Consumed consumed(views);
    return out(detail::make_menu(take_all(views)));
    ELG_CATCH(nullptr)
}

/// Wraps `MenuButton()`: a button that opens the menu `menu` builds, in a popup window.
///
/// @param take_label The button's label (consumed); null: `title` is.
/// @param title A string prop for the label, read when `take_label` is null (borrowed; its closure is taken
///   over); null: `take_label` is. One of the two is required: both null is a usage error.
/// @param menu Builds the menu, usually with elg_menu(). ret: View*. Required: none is a usage error.
/// @param placement Where the menu opens (borrowed; required).
/// @param shows_indicator A bool prop: the chevron after the label.
/// @param popup_motion How the menu opens and closes; `has` false: the MenuStyle's.
/// @return The menu button, owned by the caller, or null on failure.
ELG_EXPORT View* elg_menu_button(View* take_label, const ElgProp* title, ElgClosure menu, const ElgFloatingPlacement* placement,
                                 ElgProp shows_indicator, ElgPopupMotion popup_motion) {
    ELG_TRY
    ElgProp label_title = title ? *title : ElgProp{};
    const Consumed consumed(take_label, label_title, menu, shows_indicator);
    if (!take_label && !title) {
        throw UsageError("elg_menu_button needs take_label or title");
    }
    MenuButtonOptions options;
    options.placement = elg_shim::placement(*placement);
    options.shows_indicator = prop<bool>(shows_indicator);
    options.motion = motion(popup_motion);
    if (take_label) {
        return out(MenuButton(take(take_label), view_builder(required(menu, "menu")), std::move(options)));
    }
    return out(MenuButton(prop<std::string>(label_title), view_builder(required(menu, "menu")), std::move(options)));
    ELG_CATCH(nullptr)
}

// --- Navigation ---

/// How a split view's dividers look (`SplitViewOptions`).
struct ElgSplitViewOptions {
    ElgProp divider;               ///< A Color prop: the line.
    const ElgProp* active_divider; ///< A Color prop for while it's hovered or dragged; null: the tint.
    float thickness;               ///< The line's thickness, in points.
    float active_thickness;        ///< Drawn over the line while it's hovered or dragged, in points.
};

// `divider` and `active_divider`: the caller's copies of o's props, which its Consumed guards
// (`active_divider` empty when `o.active_divider` is null).
static SplitViewOptions split_view_options(const ElgSplitViewOptions& o, ElgProp& divider, ElgProp& active_divider) {
    SplitViewOptions result;
    result.divider = prop<Color>(divider);
    if (o.active_divider) {
        result.active_divider = prop<Color>(active_divider);
    }
    result.thickness = o.thickness;
    result.active_thickness = o.active_thickness;
    return result;
}

/// Wraps `HSplitView()` and `VSplitView()`: panes with a divider between each two; dragging a divider resizes
/// the panes on either side of it.
///
/// @param axis `Axis`: 0 side by side (HSplitView), 1 top to bottom (VSplitView).
/// @param panes The panes (consumed).
/// @param count How many `panes` there are.
/// @param options How the dividers look (borrowed; required).
/// @return The split view, owned by the caller, or null on failure.
ELG_EXPORT View* elg_split_view(int32_t axis, View** panes, size_t count, const ElgSplitViewOptions* options) {
    ELG_TRY
    ViewArray views{panes, count};
    ElgProp divider = options->divider;
    ElgProp active_divider = options->active_divider ? *options->active_divider : ElgProp{};
    const Consumed consumed(views, divider, active_divider);
    return out(detail::make_split_view(static_cast<Axis>(axis), take_all(views),
                                       split_view_options(*options, divider, active_divider)));
    ELG_CATCH(nullptr)
}

/// A column's width range, in points (`ColumnWidth`).
struct ElgColumnWidth {
    float min, ideal, max; ///< The narrowest, starting and widest width.
};

/// The options of elg_navigation_split_view(): `NavigationSplitViewOptions`.
struct ElgNavigationSplitViewOptions {
    Binding<bool>* take_sidebar_visible;  ///< Shows and hides the sidebar (consumed); null: always shown.
    const Transition* sidebar_transition; ///< How the sidebar comes and goes (borrowed; required).
    ElgProp sidebar_background;           ///< A Color prop: behind the sidebar; clear: none.
    ElgColumnWidth sidebar_width;         ///< The sidebar's width range.
    ElgColumnWidth content_width;         ///< The content column's width range (the three-column form only).
    ElgColumnWidth detail_width;          ///< The detail's width range; an ideal of 0: the rest of the width.
    ElgSplitViewOptions dividers;         ///< How the dividers look.
};

/// Wraps `NavigationSplitView()`: a sidebar, optionally a content column, and the detail of what's chosen,
/// with draggable dividers between them. The detail takes the rest of the width.
///
/// @param take_sidebar The sidebar (consumed).
/// @param take_content The content column (consumed); null: the two-column form.
/// @param take_detail The detail (consumed).
/// @param o The options (borrowed; required). Their binding and closures are taken over.
/// @return The split view, owned by the caller, or null on failure.
ELG_EXPORT View* elg_navigation_split_view(View* take_sidebar, View* take_content, View* take_detail,
                                           const ElgNavigationSplitViewOptions* o) {
    ELG_TRY
    ElgNavigationSplitViewOptions c = *o;
    ElgProp active_divider = c.dividers.active_divider ? *c.dividers.active_divider : ElgProp{};
    const Consumed consumed(take_sidebar, take_content, take_detail, c.take_sidebar_visible, c.sidebar_background,
                            c.dividers.divider, active_divider);
    NavigationSplitViewOptions options;
    if (c.take_sidebar_visible) {
        const std::unique_ptr<Binding<bool>> visible = own(c.take_sidebar_visible);
        options.sidebar_visible = *visible;
    }
    options.sidebar_transition = *c.sidebar_transition;
    options.sidebar_background = prop<Color>(c.sidebar_background);
    const auto width = [](ElgColumnWidth w) { return ColumnWidth{w.min, w.ideal, w.max}; };
    options.sidebar_width = width(c.sidebar_width);
    options.content_width = width(c.content_width);
    options.detail_width = width(c.detail_width);
    options.dividers = split_view_options(c.dividers, c.dividers.divider, active_divider);
    if (take_content) {
        return out(NavigationSplitView(take(take_sidebar), take(take_content), take(take_detail), options));
    }
    return out(NavigationSplitView(take(take_sidebar), take(take_detail), options));
    ELG_CATCH(nullptr)
}

/// Wraps `NavigationStack()` over a path kept in Rust: the root, then a page for each route in the path. Only
/// the last is shown; its look around the pages comes from the NavigationStackStyle.
///
/// @param take_root The root page (consumed).
/// @param pages A page per route in the path; a tracked read of the path, as NavigationStack does. ret:
///   std::vector<ForEachItem>* (filled with elg_for_each_items_push). Required: none is a usage error.
/// @param titles The root's title, then each pushed page's, for the style; a tracked read of the path. ret:
///   std::vector<std::string>* (filled with elg_strings_push). Required: none is a usage error.
/// @param pop Removes the last route from the path (Alt+Left, the back gestures, a style's back button). No
///   args. Required: none is a usage error.
/// @param transition `PageTransition`: 0 slide, 1 fade, 2 none.
/// @param animation For pushes and pops that aren't animated themselves (borrowed); null: they change at once.
/// @param back_gestures Alt+Left, and swiping right with two fingers on a trackpad (macOS), go back.
/// @return The navigation stack, owned by the caller, or null on failure.
ELG_EXPORT View* elg_navigation_stack(View* take_root, ElgClosure pages, ElgClosure titles, ElgClosure pop,
                                      int32_t transition, const Animation* animation, bool back_gestures) {
    ELG_TRY
    const Consumed consumed(take_root, pages, titles, pop);
    const SharedFn p = required(pages, "pages");
    const SharedFn t = required(titles, "titles");
    const std::function<void()> back = action(required(pop, "pop"));
    return out(detail::make_navigation_stack(
        take(take_root),
        [p] {
            std::vector<detail::ForEachItem> result;
            p->call(nullptr, &result);
            return result;
        },
        [t] {
            std::vector<std::string> result;
            t->call(nullptr, &result);
            return result;
        },
        back, {static_cast<PageTransition>(transition), elg_shim::animation(animation), back_gestures}));
    ELG_CATCH(nullptr)
}

/// Appends a string to a closure's `std::vector<std::string>` result (elg_navigation_stack()'s `titles`).
///
/// @param target The closure's `ret`.
/// @param value The string (copied).
ELG_EXPORT void elg_strings_push(std::vector<std::string>* target, ElgStr value) {
    ELG_TRY
    target->push_back(str(value));
    ELG_CATCH_VOID
}

// --- Text ---

/// Wraps `Text()`: text in one font and color, wrapping at word boundaries to the proposed width.
///
/// @param content A string prop: the text.
/// @return The text, owned by the caller (free it with elg_text_free()), or null on failure.
ELG_EXPORT Text* elg_text(ElgProp content) {
    ELG_TRY
    const Consumed consumed(content);
    return new Text(prop<std::string>(content));
    ELG_CATCH(nullptr)
}
/// Copies a Text.
///
/// @param text The text to copy (borrowed).
/// @return The copy, owned by the caller. Null on failure.
ELG_EXPORT Text* elg_text_clone(const Text* text) {
    ELG_TRY
    return new Text(*text);
    ELG_CATCH(nullptr)
}
/// Frees a Text.
///
/// @param text The text (consumed); may be null.
ELG_EXPORT void elg_text_free(Text* text) {
    ELG_TRY
    delete text;
    ELG_CATCH_VOID
}
/// A Text as a View, for containers and view modifiers.
///
/// @param text The text (borrowed).
/// @return The view, owned by the caller. Null on failure.
ELG_EXPORT View* elg_text_to_view(const Text* text) {
    ELG_TRY
    return new View(*text);
    ELG_CATCH(nullptr)
}
/// Wraps `Text::font()`.
///
/// @param text The text (borrowed).
/// @param f A Font prop (ElgFont).
/// @return `text` in that font (a new Text, owned by the caller), or null on failure.
ELG_EXPORT Text* elg_text_font(const Text* text, ElgProp f) {
    ELG_TRY
    const Consumed consumed(f);
    return new Text(text->font(prop<Font>(f)));
    ELG_CATCH(nullptr)
}
/// Wraps `Text::font_weight()`.
///
/// @param text The text (borrowed).
/// @param weight A Weight prop (int32_t, 1...1000).
/// @return `text` in that weight (a new Text, owned by the caller), or null on failure.
ELG_EXPORT Text* elg_text_font_weight(const Text* text, ElgProp weight) {
    ELG_TRY
    const Consumed consumed(weight);
    return new Text(text->font_weight(prop<Weight>(weight)));
    ELG_CATCH(nullptr)
}
/// Wraps `Text::bold()`.
///
/// @param text The text (borrowed).
/// @return `text` in bold (a new Text, owned by the caller), or null on failure.
ELG_EXPORT Text* elg_text_bold(const Text* text) {
    ELG_TRY
    return new Text(text->bold());
    ELG_CATCH(nullptr)
}
/// Wraps `Text::foreground_style()`.
///
/// @param text The text (borrowed).
/// @param color A Color prop.
/// @return `text` in that color (a new Text, owned by the caller), or null on failure.
ELG_EXPORT Text* elg_text_foreground_style(const Text* text, ElgProp color) {
    ELG_TRY
    const Consumed consumed(color);
    return new Text(text->foreground_style(prop<Color>(color)));
    ELG_CATCH(nullptr)
}
/// Wraps `Text::line_limit()`: the text truncates with an ellipsis past this many lines.
///
/// @param text The text (borrowed).
/// @param lines The most lines it takes; 0: no limit.
/// @return `text` with the limit (a new Text, owned by the caller), or null on failure.
ELG_EXPORT Text* elg_text_line_limit(const Text* text, int32_t lines) {
    ELG_TRY
    return new Text(text->line_limit(lines));
    ELG_CATCH(nullptr)
}
/// Wraps `Text::multiline_text_alignment()`: how lines align when the text wraps.
///
/// @param text The text (borrowed).
/// @param alignment `TextAlignment`: 0 leading, 1 center, 2 trailing.
/// @return `text` with that alignment (a new Text, owned by the caller), or null on failure.
ELG_EXPORT Text* elg_text_multiline_alignment(const Text* text, int32_t alignment) {
    ELG_TRY
    return new Text(text->multiline_text_alignment(static_cast<TextAlignment>(alignment)));
    ELG_CATCH(nullptr)
}

/// One run of elg_styled_text() (`TextRun`).
struct ElgTextRun {
    ElgStr text;       ///< The run's text (copied); newlines start paragraphs.
    ElgRunStyle style; ///< How it looks; unset parts come from the environment.
    ElgClosure on_tap; ///< Runs when the run is clicked (links, mentions). No args; none: not clickable.
    /// Shown in the line instead of the text, which stays the accessible label (consumed); null: none.
    View* take_attachment;
    float attachment_size; ///< The attachment's square, in ems; it's centered on the line.
};

static void release_held(ElgTextRun& run) noexcept {
    release(run.on_tap);
    delete std::exchange(run.take_attachment, nullptr);
}

/// Wraps `StyledText()`: text made of runs in different weights, colors and highlights, wrapping like Text.
///
/// @param runs The runs (borrowed; their closures and attachments are taken over).
/// @param count How many `runs` there are.
/// @return The view, owned by the caller, or null on failure.
ELG_EXPORT View* elg_styled_text(const ElgTextRun* runs, size_t count) {
    ELG_TRY
    ConsumedElements<ElgTextRun> elements(runs, count, &release_held);
    std::vector<TextRun> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        ElgTextRun c = elements.next();
        const Consumed consumed(c.on_tap, c.take_attachment);
        TextRun run;
        run.text = str(c.text);
        run.style = run_style(c.style);
        run.on_tap = action(share(c.on_tap));
        if (c.take_attachment) {
            run.attachment = take(c.take_attachment);
        }
        run.attachment_size = c.attachment_size;
        result.push_back(std::move(run));
    }
    return out(StyledText(std::move(result)));
    ELG_CATCH(nullptr)
}

// --- Shapes ---

/// Copies a Shape.
///
/// @param shape The shape to copy (borrowed).
/// @return The copy, owned by the caller. Null on failure.
ELG_EXPORT Shape* elg_shape_clone(const Shape* shape) {
    ELG_TRY
    return new Shape(*shape);
    ELG_CATCH(nullptr)
}
/// Frees a Shape.
///
/// @param shape The shape (consumed); may be null.
ELG_EXPORT void elg_shape_free(Shape* shape) {
    ELG_TRY
    delete shape;
    ELG_CATCH_VOID
}
/// A Shape as a View, for containers and view modifiers.
///
/// @param shape The shape (borrowed).
/// @return The view, owned by the caller. Null on failure.
ELG_EXPORT View* elg_shape_to_view(const Shape* shape) {
    ELG_TRY
    return new View(*shape);
    ELG_CATCH(nullptr)
}
/// Wraps `Rectangle()`: a shape filling its frame.
///
/// @return The shape, owned by the caller (free it with elg_shape_free()). Null on failure.
ELG_EXPORT Shape* elg_rectangle() {
    ELG_TRY
    return new Shape(Rectangle());
    ELG_CATCH(nullptr)
}
/// Wraps `RoundedRectangle()` with a radius per corner (`CornerRadii`). Radii are in points.
///
/// @param top_left The top-left corner's radius.
/// @param top_right The top-right corner's radius.
/// @param bottom_right The bottom-right corner's radius.
/// @param bottom_left The bottom-left corner's radius.
/// @return The shape, owned by the caller, or null on failure.
ELG_EXPORT Shape* elg_rounded_rectangle(float top_left, float top_right, float bottom_right, float bottom_left) {
    ELG_TRY
    return new Shape(RoundedRectangle(CornerRadii{top_left, top_right, bottom_right, bottom_left}));
    ELG_CATCH(nullptr)
}
/// Wraps `Capsule()`: fully rounded ends along the shorter side.
///
/// @return The shape, owned by the caller. Null on failure.
ELG_EXPORT Shape* elg_capsule() {
    ELG_TRY
    return new Shape(Capsule());
    ELG_CATCH(nullptr)
}
/// Wraps `Circle()`: the largest circle that fits, centered.
///
/// @return The shape, owned by the caller. Null on failure.
ELG_EXPORT Shape* elg_circle() {
    ELG_TRY
    return new Shape(Circle());
    ELG_CATCH(nullptr)
}
/// Wraps `PathShape()`: a vector path as a shape.
///
/// @param path The path (borrowed; copied).
/// @param view_box The path's own coordinate box, as in SVG: the path is scaled to fit the frame, keeping its
///   aspect ratio, and centered (borrowed). Null: path coordinates are points from the frame's corner.
/// @param fill_rule `FillRule`: 0 non-zero, 1 even-odd.
/// @return The shape, owned by the caller, or null on failure.
ELG_EXPORT Shape* elg_path_shape(const Path* path, const ElgRect* view_box, int32_t fill_rule) {
    ELG_TRY
    PathShapeOptions options;
    if (view_box) {
        options.view_box = rect(*view_box);
    }
    options.fill_rule = static_cast<FillRule>(fill_rule);
    return new Shape(PathShape(*path, options));
    ELG_CATCH(nullptr)
}
/// Wraps `CustomShape()`: a shape whose path depends on its frame, rebuilt when the frame's size changes.
///
/// @param path Builds the outline for the frame's size, given as a rect at (0, 0). args: ElgRect*, ret: Path*
///   (written with elg_path_assign). Required: none is a usage error.
/// @param fill_rule `FillRule`: 0 non-zero, 1 even-odd.
/// @return The shape, owned by the caller, or null on failure.
ELG_EXPORT Shape* elg_custom_shape(ElgClosure path, int32_t fill_rule) {
    ELG_TRY
    const Consumed consumed(path);
    const SharedFn f = required(path, "path");
    return new Shape(CustomShape(
        [f](Rect bounds) {
            ElgRect arg = c_rect(bounds);
            Path result;
            f->call(&arg, &result);
            return result;
        },
        static_cast<FillRule>(fill_rule)));
    ELG_CATCH(nullptr)
}
/// Wraps `Shape::fill()`.
///
/// @param shape The shape (borrowed).
/// @param style A ShapeStyle prop (ElgShapeStyle): a color or gradient.
/// @return `shape` filled with `style` (a new Shape, owned by the caller), or null on failure.
ELG_EXPORT Shape* elg_shape_fill(const Shape* shape, ElgProp style) {
    ELG_TRY
    const Consumed consumed(style);
    return new Shape(shape->fill(prop<ShapeStyle>(style)));
    ELG_CATCH(nullptr)
}
/// Wraps `Shape::stroke()` with a `StrokeStyle`: a line centered on the shape's edge, with caps, joins and
/// dashes.
///
/// @param shape The shape (borrowed).
/// @param color A Color prop: the line's color.
/// @param style The line's width, caps, joins and dashes (borrowed).
/// @return `shape` with the line (a new Shape, owned by the caller), or null on failure.
ELG_EXPORT Shape* elg_shape_stroke(const Shape* shape, ElgProp color, const ElgStrokeStyle* style) {
    ELG_TRY
    const Consumed consumed(color);
    return new Shape(shape->stroke(prop<Color>(color), stroke_style(*style)));
    ELG_CATCH(nullptr)
}
/// Wraps `Shape::stroke_border()`: a line inside the shape's edge.
///
/// @param shape The shape (borrowed).
/// @param color A Color prop: the line's color.
/// @param line_width The line's width, in points.
/// @return `shape` with the line (a new Shape, owned by the caller), or null on failure.
ELG_EXPORT Shape* elg_shape_stroke_border(const Shape* shape, ElgProp color, float line_width) {
    ELG_TRY
    const Consumed consumed(color);
    return new Shape(shape->stroke_border(prop<Color>(color), line_width));
    ELG_CATCH(nullptr)
}

// --- Paths ---

/// Wraps `Path()`: an empty path, to build with the elg_path_* functions.
///
/// @return The path, owned by the caller (free it with elg_path_free()). Null on failure.
ELG_EXPORT Path* elg_path_new() {
    ELG_TRY
    return new Path();
    ELG_CATCH(nullptr)
}
/// Copies a Path.
///
/// @param path The path to copy (borrowed).
/// @return The copy, owned by the caller. Null on failure.
ELG_EXPORT Path* elg_path_clone(const Path* path) {
    ELG_TRY
    return new Path(*path);
    ELG_CATCH(nullptr)
}
/// Frees a Path.
///
/// @param path The path (consumed); may be null.
ELG_EXPORT void elg_path_free(Path* path) {
    ELG_TRY
    delete path;
    ELG_CATCH_VOID
}
/// Copies `source` into `target`, e.g. into the Path result of elg_custom_shape()'s closure.
///
/// @param target The path to overwrite.
/// @param source The path to copy (borrowed).
ELG_EXPORT void elg_path_assign(Path* target, const Path* source) {
    ELG_TRY
    *target = *source;
    ELG_CATCH_VOID
}
/// Wraps Path's `operator==`.
///
/// @param a A path (borrowed).
/// @param b Another path (borrowed).
/// @return Whether the two are the same path, segment for segment. False on failure.
ELG_EXPORT bool elg_path_eq(const Path* a, const Path* b) {
    ELG_TRY
    return *a == *b;
    ELG_CATCH(false)
}
/// Wraps `Path::move_to()`: starts a new subpath at `p`.
///
/// @param path The path to add to.
/// @param p Where the subpath starts.
ELG_EXPORT void elg_path_move_to(Path* path, ElgPoint p) {
    ELG_TRY
    path->move_to(point(p));
    ELG_CATCH_VOID
}
/// Wraps `Path::line_to()`: a straight line to `p`.
///
/// @param path The path to add to.
/// @param p Where the line ends.
ELG_EXPORT void elg_path_line_to(Path* path, ElgPoint p) {
    ELG_TRY
    path->line_to(point(p));
    ELG_CATCH_VOID
}
/// Wraps `Path::quad_to()`: a quadratic Bézier curve to `end`.
///
/// @param path The path to add to.
/// @param control The control point.
/// @param end Where the curve ends.
ELG_EXPORT void elg_path_quad_to(Path* path, ElgPoint control, ElgPoint end) {
    ELG_TRY
    path->quad_to(point(control), point(end));
    ELG_CATCH_VOID
}
/// Wraps `Path::cubic_to()`: a cubic Bézier curve to `end`.
///
/// @param path The path to add to.
/// @param control1 The first control point.
/// @param control2 The second control point.
/// @param end Where the curve ends.
ELG_EXPORT void elg_path_cubic_to(Path* path, ElgPoint control1, ElgPoint control2, ElgPoint end) {
    ELG_TRY
    path->cubic_to(point(control1), point(control2), point(end));
    ELG_CATCH_VOID
}
/// Wraps `Path::arc_to()`: an elliptical arc to `end`, as in SVG.
///
/// @param path The path to add to.
/// @param rx The ellipse's horizontal radius.
/// @param ry The ellipse's vertical radius.
/// @param rotation The ellipse's x-axis rotation, in degrees.
/// @param large_arc Take the longer of the two arcs.
/// @param sweep Go clockwise.
/// @param end Where the arc ends.
ELG_EXPORT void elg_path_arc_to(Path* path, float rx, float ry, float rotation, bool large_arc, bool sweep, ElgPoint end) {
    ELG_TRY
    path->arc_to(rx, ry, rotation, large_arc, sweep, point(end));
    ELG_CATCH_VOID
}
/// Wraps `Path::close()`: closes the current subpath with a line back to its start.
///
/// @param path The path to add to.
ELG_EXPORT void elg_path_close(Path* path) {
    ELG_TRY
    path->close();
    ELG_CATCH_VOID
}
/// Wraps `Path::add_rect()`: a rectangle as a closed subpath.
///
/// @param path The path to add to.
/// @param r The rectangle.
ELG_EXPORT void elg_path_add_rect(Path* path, ElgRect r) {
    ELG_TRY
    path->add_rect(rect(r));
    ELG_CATCH_VOID
}
/// Wraps `Path::add_rounded_rect()`: a rounded rectangle as a closed subpath.
///
/// @param path The path to add to.
/// @param r The rectangle.
/// @param radius The corners' radius.
ELG_EXPORT void elg_path_add_rounded_rect(Path* path, ElgRect r, float radius) {
    ELG_TRY
    path->add_rounded_rect(rect(r), radius);
    ELG_CATCH_VOID
}
/// Wraps `Path::add_ellipse()`: the ellipse inside `r` as a closed subpath.
///
/// @param path The path to add to.
/// @param r The rectangle the ellipse fills.
ELG_EXPORT void elg_path_add_ellipse(Path* path, ElgRect r) {
    ELG_TRY
    path->add_ellipse(rect(r));
    ELG_CATCH_VOID
}
/// Wraps `Path::add_circle()`: a circle as a closed subpath.
///
/// @param path The path to add to.
/// @param center The circle's center.
/// @param radius The circle's radius.
ELG_EXPORT void elg_path_add_circle(Path* path, ElgPoint center, float radius) {
    ELG_TRY
    path->add_circle(point(center), radius);
    ELG_CATCH_VOID
}
/// Wraps `Path::add_path()`: appends another path's subpaths.
///
/// @param path The path to add to.
/// @param other The path to append (borrowed).
ELG_EXPORT void elg_path_add_path(Path* path, const Path* other) {
    ELG_TRY
    path->add_path(*other);
    ELG_CATCH_VOID
}
/// Wraps `Path::svg()`: parses SVG path data ("M10 10 L20 20 Z", all commands, relative and absolute).
///
/// @param data The path data.
/// @return The path, owned by the caller, or null on failure: a UsageError with the position of anything it
///   can't read.
ELG_EXPORT Path* elg_path_svg(ElgStr data) {
    ELG_TRY
    return new Path(Path::svg(view(data)));
    ELG_CATCH(nullptr)
}
/// Wraps `Path::empty()`.
///
/// @param path The path (borrowed).
/// @return Whether the path has nothing in it. False on failure.
ELG_EXPORT bool elg_path_empty(const Path* path) {
    ELG_TRY
    return path->empty();
    ELG_CATCH(false)
}
/// Wraps `Path::bounds()`.
///
/// @param path The path (borrowed).
/// @return The smallest rect around the path's outline (curves included, not their control points). Zero on failure.
ELG_EXPORT ElgRect elg_path_bounds(const Path* path) {
    ELG_TRY
    return c_rect(path->bounds());
    ELG_CATCH({})
}
/// Wraps `Path::contains()`.
///
/// @param path The path (borrowed).
/// @param p The point to test.
/// @param rule `FillRule`: 0 non-zero, 1 even-odd.
/// @return Whether the path's outline encloses `p`. False on failure.
ELG_EXPORT bool elg_path_contains(const Path* path, ElgPoint p, int32_t rule) {
    ELG_TRY
    return path->contains(point(p), static_cast<FillRule>(rule));
    ELG_CATCH(false)
}
/// Wraps `Path::transformed()`: the path scaled, then moved: (x * sx + dx, y * sy + dy).
///
/// @param path The path (borrowed).
/// @param sx The horizontal scale.
/// @param sy The vertical scale.
/// @param dx The horizontal move, after scaling.
/// @param dy The vertical move, after scaling.
/// @return The new path, owned by the caller. Null on failure.
ELG_EXPORT Path* elg_path_transformed(const Path* path, float sx, float sy, float dx, float dy) {
    ELG_TRY
    return new Path(path->transformed(sx, sy, dx, dy));
    ELG_CATCH(nullptr)
}
/// One flattened subpath (`Path::Polyline`): points joined by straight lines. What elg_path_flatten()'s
/// `polyline` gets.
struct ElgPolyline {
    const ElgPoint* points; ///< The points, valid during the call.
    size_t count;           ///< How many `points` there are.
    bool closed;            ///< The last point joins the first.
};
/// Wraps `Path::flatten()`: lines approximating the path's curves.
///
/// @param path The path (borrowed).
/// @param tolerance The farthest the lines may stray from the curves, in points.
/// @param polyline Called once per flattened subpath, during this call. args: ElgPolyline*. Required: none is a
///   usage error.
ELG_EXPORT void elg_path_flatten(const Path* path, float tolerance, ElgClosure polyline) {
    ELG_TRY
    const Consumed consumed(polyline);
    const SharedFn f = required(polyline, "polyline");
    for (const Path::Polyline& line : path->flatten(tolerance)) {
        static_assert(sizeof(Point) == sizeof(ElgPoint));
        ElgPolyline arg{reinterpret_cast<const ElgPoint*>(line.points.data()), line.points.size(), line.closed};
        f->call(&arg, nullptr);
    }
    ELG_CATCH_VOID
}
/// Wraps `Path::stroked()`: the outline of the path's stroke, to fill with the non-zero rule.
///
/// @param path The path (borrowed).
/// @param style The stroke's width, caps, joins and dashes (borrowed).
/// @param tolerance How far curves may be flattened from their true shape first, in points.
/// @return The outline, owned by the caller, or null on failure.
ELG_EXPORT Path* elg_path_stroked(const Path* path, const ElgStrokeStyle* style, float tolerance) {
    ELG_TRY
    return new Path(path->stroked(stroke_style(*style), tolerance));
    ELG_CATCH(nullptr)
}

// --- Drawing ---

/// Wraps `Canvas()`: immediate-mode drawing. It takes the proposed size.
///
/// @param draw Runs every time the view is drawn. args: GraphicsContext* (valid during the call; draw with
///   the elg_gc_* functions). Required: none is a usage error.
/// @return The canvas, owned by the caller, or null on failure.
ELG_EXPORT View* elg_canvas(ElgClosure draw) {
    ELG_TRY
    const Consumed consumed(draw);
    const SharedFn f = required(draw, "draw");
    return out(Canvas([f](GraphicsContext& context) { f->call(&context, nullptr); }));
    ELG_CATCH(nullptr)
}
/// Wraps `GraphicsContext::size()`.
///
/// @param gc The context a Canvas's `draw` got.
/// @return The drawable area, in points. Zero on failure.
ELG_EXPORT ElgSize elg_gc_size(const GraphicsContext* gc) {
    ELG_TRY
    const Size s = gc->size();
    return {s.width, s.height};
    ELG_CATCH({})
}
/// Wraps `GraphicsContext::scale()`.
///
/// @param gc The context a Canvas's `draw` got.
/// @return Pixels per point. 0 on failure.
ELG_EXPORT float elg_gc_scale(const GraphicsContext* gc) {
    ELG_TRY
    return gc->scale();
    ELG_CATCH(0)
}
/// Wraps `GraphicsContext::time()`: the frame clock, the same instant for everything drawn in a frame.
///
/// @param gc The context a Canvas's `draw` got.
/// @return The frame clock's time, in seconds. 0 on failure.
ELG_EXPORT double elg_gc_time(const GraphicsContext* gc) {
    ELG_TRY
    return gc->time();
    ELG_CATCH(0)
}
/// Wraps `GraphicsContext::request_frame()`: draws the canvas again next frame. Call it on every draw while the
/// drawing should keep moving; the window goes back to idle once nothing asks.
///
/// @param gc The context a Canvas's `draw` got.
ELG_EXPORT void elg_gc_request_frame(GraphicsContext* gc) {
    ELG_TRY
    gc->request_frame();
    ELG_CATCH_VOID
}
/// Wraps `GraphicsContext::fill_rect()`.
///
/// @param gc The context a Canvas's `draw` got.
/// @param r The rectangle, in the canvas's points.
/// @param c The fill color.
/// @param radii Four corner radii: top-left, top-right, bottom-right, bottom-left (required).
ELG_EXPORT void elg_gc_fill_rect(GraphicsContext* gc, ElgRect r, ElgColor c, const float* radii) {
    ELG_TRY
    gc->fill_rect(rect(r), color(c), CornerRadii{radii[0], radii[1], radii[2], radii[3]});
    ELG_CATCH_VOID
}
/// Wraps `GraphicsContext::stroke_rect()`: a border inside `r`.
///
/// @param gc The context a Canvas's `draw` got.
/// @param r The rectangle, in the canvas's points.
/// @param c The border's color.
/// @param width The border's width, in points.
/// @param radii Four corner radii: top-left, top-right, bottom-right, bottom-left (required).
ELG_EXPORT void elg_gc_stroke_rect(GraphicsContext* gc, ElgRect r, ElgColor c, float width, const float* radii) {
    ELG_TRY
    gc->stroke_rect(rect(r), color(c), width, CornerRadii{radii[0], radii[1], radii[2], radii[3]});
    ELG_CATCH_VOID
}
/// Wraps `GraphicsContext::draw_rect()`: a fill and a border in one.
///
/// @param gc The context a Canvas's `draw` got.
/// @param r The rectangle, in the canvas's points.
/// @param radii Four corner radii: top-left, top-right, bottom-right, bottom-left (required).
/// @param fill The fill color.
/// @param border The border's color.
/// @param border_width The border's width, in points.
ELG_EXPORT void elg_gc_draw_rect(GraphicsContext* gc, ElgRect r, const float* radii, ElgColor fill, ElgColor border,
                                 float border_width) {
    ELG_TRY
    gc->draw_rect(rect(r), CornerRadii{radii[0], radii[1], radii[2], radii[3]}, color(fill), color(border), border_width);
    ELG_CATCH_VOID
}
/// Wraps `GraphicsContext::fill_path()`.
///
/// @param gc The context a Canvas's `draw` got.
/// @param path The path, in the canvas's points (borrowed).
/// @param style A color or gradient (borrowed).
/// @param rule `FillRule`: 0 non-zero, 1 even-odd.
ELG_EXPORT void elg_gc_fill_path(GraphicsContext* gc, const Path* path, const ElgShapeStyle* style, int32_t rule) {
    ELG_TRY
    gc->fill_path(*path, shape_style(*style), static_cast<FillRule>(rule));
    ELG_CATCH_VOID
}
/// Wraps `GraphicsContext::stroke_path()`.
///
/// @param gc The context a Canvas's `draw` got.
/// @param path The path, in the canvas's points (borrowed).
/// @param c The line's color.
/// @param style The line's width, caps, joins and dashes (borrowed).
ELG_EXPORT void elg_gc_stroke_path(GraphicsContext* gc, const Path* path, ElgColor c, const ElgStrokeStyle* style) {
    ELG_TRY
    gc->stroke_path(*path, color(c), stroke_style(*style));
    ELG_CATCH_VOID
}
/// Wraps `GraphicsContext::push_clip()`: drawing until the matching elg_gc_pop_clip() shows only inside `r`.
/// Clips nest; ones left pushed are popped when the drawing ends.
///
/// @param gc The context a Canvas's `draw` got.
/// @param r The clip, in the canvas's points.
/// @param radii Four corner radii: top-left, top-right, bottom-right, bottom-left (required).
ELG_EXPORT void elg_gc_push_clip(GraphicsContext* gc, ElgRect r, const float* radii) {
    ELG_TRY
    gc->push_clip(rect(r), CornerRadii{radii[0], radii[1], radii[2], radii[3]});
    ELG_CATCH_VOID
}
/// Wraps `GraphicsContext::pop_clip()`: ends the last clip pushed.
///
/// @param gc The context a Canvas's `draw` got.
ELG_EXPORT void elg_gc_pop_clip(GraphicsContext* gc) {
    ELG_TRY
    gc->pop_clip();
    ELG_CATCH_VOID
}

// --- Images ---

/// Wraps `Image()`: an icon, an image file, or image data. Raster images decode in the background and are
/// drawn at their own size unless made resizable; one that can't be read or decoded logs a warning and shows
/// nothing.
///
/// @param source What it shows: set exactly one of `system_name`, `file` and `data` (borrowed; data is
///   copied).
/// @return The image, owned by the caller, or null on failure: a UsageError for an unknown icon.
ELG_EXPORT View* elg_image(const ElgImageSource* source) {
    VIEW_TRY(Image(image_source(*source)))
}

/// Wraps `Image()` with an icon name: "set.icon", or "icon" in the default set, with ".variant" after either.
///
/// @param system_name The icon's name.
/// @return The image, owned by the caller, or null on failure: a UsageError for an unknown icon.
ELG_EXPORT View* elg_image_named(ElgStr system_name) {
    VIEW_TRY(Image(view(system_name)))
}

// content_mode < 0: none.
static AsyncImageOptions async_image_options(float scale, int32_t content_mode, bool disk_cache) {
    AsyncImageOptions options;
    options.scale = scale;
    if (content_mode >= 0) {
        options.content_mode = static_cast<ContentMode>(content_mode);
    }
    options.disk_cache = disk_cache;
    return options;
}

/// Wraps `AsyncImage()`: loads an image over HTTP(S) when first shown: a spinner while loading, the image once
/// it's here, a placeholder icon if it fails. Downloads are cached in memory and on disk (elg_set_image_cache).
///
/// @param url The image's URL (copied).
/// @param scale Pixels per point.
/// @param content_mode Resizable, keeping its aspect ratio this way (`ContentMode`: 0 fit, 1 fill); negative:
///   at its own size.
/// @param disk_cache false: kept in memory only, never in the disk cache.
/// @return The image, owned by the caller, or null on failure.
ELG_EXPORT View* elg_async_image(ElgStr url, float scale, int32_t content_mode, bool disk_cache) {
    VIEW_TRY(AsyncImage(str(url), async_image_options(scale, content_mode, disk_cache)))
}

/// Wraps `set_image_cache()`: AsyncImage's disk cache. Downloads already under way use the cache they started
/// with. UI thread only.
///
/// @param disk Whether downloads are kept on disk between runs.
/// @param disk_limit The most the disk cache holds, in bytes; the least recently used go first past it.
/// @param directory Where the disk cache is; empty: the default, in the user's cache folder.
ELG_EXPORT void elg_set_image_cache(bool disk, uint64_t disk_limit, ElgStr directory) {
    ELG_TRY
    set_image_cache({disk, disk_limit, path(directory)});
    ELG_CATCH_VOID
}

/// Wraps `clear_image_cache()`: forgets every downloaded image, in memory and on disk. Downloads under way
/// finish without being cached. UI thread only.
ELG_EXPORT void elg_clear_image_cache() {
    ELG_TRY
    clear_image_cache();
    ELG_CATCH_VOID
}

/// Where an AsyncImage is (`AsyncImagePhase`): what elg_async_image_content()'s `content` gets.
struct ElgAsyncImagePhase {
    int32_t kind;      ///< `AsyncImagePhase::Kind`: 0 loading, 1 success, 2 failure.
    const View* image; ///< Success: the image, valid during the call (clone it to keep it).
    ElgStr error;      ///< Failure: why, valid during the call.
};

/// Wraps `AsyncImage()` with a content function: like elg_async_image(), with a view of your own for each
/// phase.
///
/// @param url The image's URL (copied).
/// @param content Builds the view for each phase. args: ElgAsyncImagePhase*, ret: View*. Required: none is a
///   usage error.
/// @param scale Pixels per point.
/// @param disk_cache false: kept in memory only, never in the disk cache.
/// @return The image, owned by the caller, or null on failure.
ELG_EXPORT View* elg_async_image_content(ElgStr url, ElgClosure content, float scale, bool disk_cache) {
    ELG_TRY
    const Consumed consumed(content);
    const SharedFn f = required(content, "content");
    return out(AsyncImage(
        str(url),
        [f](const AsyncImagePhase& phase) {
            ElgAsyncImagePhase arg{static_cast<int32_t>(phase.kind), &phase.image, c_str(phase.error)};
            View result;
            f->call(&arg, &result);
            return result;
        },
        async_image_options(scale, -1, disk_cache)));
    ELG_CATCH(nullptr)
}

/// Wraps `Label()`: an icon followed by a title, spaced for buttons, menus and lists.
///
/// @param title A string prop: the title.
/// @param icon The icon (borrowed; required).
/// @return The label, owned by the caller, or null on failure.
ELG_EXPORT View* elg_label(ElgProp title, const ElgImageSource* icon) {
    ELG_TRY
    const Consumed consumed(title);
    return out(Label(prop<std::string>(title), image_source(*icon)));
    ELG_CATCH(nullptr)
}

/// A font file, or font data in memory (`IconFont`): `data` if it's set, else `file`, else none.
struct ElgIconFont {
    ElgStr file;   ///< The font file's path.
    ElgBytes data; ///< The font data (copied when registered).
};

static IconFont icon_font(const ElgIconFont& f) {
    if (f.data.ptr) {
        return IconFont(bytes(f.data));
    }
    if (f.file.len) {
        return IconFont(path(f.file));
    }
    return IconFont();
}

/// Another weight of an icon set (`IconSetOptions::weights`), for `.font_weight()`.
struct ElgIconWeight {
    int32_t weight;   ///< The `Weight`, 1...1000.
    ElgIconFont font; ///< That weight's font.
};

/// A variant of an icon set (`IconVariant`), named "set.icon.variant".
struct ElgIconVariant {
    ElgStr name;       ///< The variant's name.
    ElgIconFont font;  ///< Its font.
    ElgStr names;      ///< Its own name map's file, when its codepoints differ from the set's; empty: the set's.
    ElgStr names_data; ///< Or that map's text (read during the call).
};

/// Wraps `register_icon_set()`: makes "name.icon" available to Image. Records a UsageError if the name is taken
/// (unless `replace`) or the options are incomplete, and a ResourceError if a file can't be read.
///
/// @param name The set's name.
/// @param font The regular weight's font.
/// @param weights More weights (borrowed).
/// @param weight_count How many `weights` there are.
/// @param variants Variants (borrowed).
/// @param variant_count How many `variants` there are.
/// @param names The name-to-codepoint map's file, as the set ships it: CSS from icon webfonts, JSON, or
///   Material-style codepoints text. The same names apply to every weight and variant.
/// @param names_data Or the map's text (read during the call).
/// @param replace Replace a set registered under the same name.
ELG_EXPORT void elg_register_icon_set(ElgStr name, ElgIconFont font, const ElgIconWeight* weights, size_t weight_count,
                                      const ElgIconVariant* variants, size_t variant_count, ElgStr names,
                                      ElgStr names_data, bool replace) {
    ELG_TRY
    IconSetOptions options;
    options.font = icon_font(font);
    for (size_t i = 0; i < weight_count; ++i) {
        options.weights.emplace_back(static_cast<Weight>(weights[i].weight), icon_font(weights[i].font));
    }
    for (size_t i = 0; i < variant_count; ++i) {
        options.variants.push_back(
            {str(variants[i].name), icon_font(variants[i].font), path(variants[i].names), view(variants[i].names_data)});
    }
    options.names = path(names);
    options.names_data = view(names_data);
    options.replace = replace;
    register_icon_set(str(name), options);
    ELG_CATCH_VOID
}

/// Wraps `set_default_icon_set()`: the set for icon names without a set prefix ("phosphor" unless changed).
///
/// @param name The set's name.
ELG_EXPORT void elg_set_default_icon_set(ElgStr name) {
    ELG_TRY
    set_default_icon_set(str(name));
    ELG_CATCH_VOID
}

// --- Fonts ---

/// Wraps `register_font()` with a file: makes it available as `Font::custom(family, ...)`. Register each
/// weight's file separately; a variable font draws every weight. Thread-safe.
///
/// @param family The family's name.
/// @param file The font file.
/// @param weight The file's `Weight`, 1...1000.
ELG_EXPORT void elg_register_font_file(ElgStr family, ElgStr file, int32_t weight) {
    ELG_TRY
    register_font(str(family), path(file), static_cast<Weight>(weight));
    ELG_CATCH_VOID
}

/// Wraps `register_font()` with font data in memory: like elg_register_font_file(). Thread-safe.
///
/// @param family The family's name.
/// @param data The font data (copied).
/// @param weight The data's `Weight`, 1...1000.
ELG_EXPORT void elg_register_font_data(ElgStr family, ElgBytes data, int32_t weight) {
    ELG_TRY
    register_font(str(family), bytes(data), static_cast<Weight>(weight));
    ELG_CATCH_VOID
}

/// Wraps `register_emoji_font()`: draws emoji with this color font (COLR/CPAL, e.g. Twemoji) before the
/// system's, in every text view. Thread-safe; register before the text is first shaped.
///
/// @param file The font file.
ELG_EXPORT void elg_register_emoji_font(ElgStr file) {
    ELG_TRY
    register_emoji_font(path(file));
    ELG_CATCH_VOID
}

/// Writes a font into a closure's Font result (the `ret` of a reactive Font prop's closure).
///
/// @param target The closure's `ret`.
/// @param value The font (borrowed).
ELG_EXPORT void elg_font_assign(Font* target, const ElgFont* value) {
    ELG_TRY
    *target = font(*value);
    ELG_CATCH_VOID
}

/// Writes a color or gradient into a closure's ShapeStyle result (the `ret` of a reactive ShapeStyle prop's
/// closure).
///
/// @param target The closure's `ret`.
/// @param value The color or gradient (borrowed).
ELG_EXPORT void elg_shape_style_assign(ShapeStyle* target, const ElgShapeStyle* value) {
    ELG_TRY
    *target = shape_style(*value);
    ELG_CATCH_VOID
}

// --- View modifiers ---

#define MOD_TRY(...)                  \
    ELG_TRY return out(__VA_ARGS__); \
    ELG_CATCH(nullptr)

/// Wraps `View::padding()` with insets.
///
/// @param v The view (borrowed).
/// @param insets An EdgeInsets prop (ElgEdgeInsets), in points.
/// @return `v` padded by `insets` (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_padding(const View* v, ElgProp insets) {
    ELG_TRY
    const Consumed consumed(insets);
    return out(v->padding(prop<EdgeInsets>(insets)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::padding()` on some edges.
///
/// @param v The view (borrowed).
/// @param edges `Edges` bits: 1 top, 2 leading, 4 bottom, 8 trailing.
/// @param length A float prop: the padding, in points.
/// @return `v` padded on `edges` (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_padding_edges(const View* v, uint32_t edges, ElgProp length) {
    ELG_TRY
    const Consumed consumed(length);
    return out(v->padding(static_cast<Edges>(edges), prop<float>(length)));
    ELG_CATCH(nullptr)
}

/// The options of elg_mod_frame(): `FrameOptions`, or `FlexibleFrameOptions` when any minimum, ideal or
/// maximum is set. A fixed dimension in a flexible frame is its minimum, ideal and maximum.
struct ElgFrameOptions {
    ElgOptLen width, height;                        ///< Fixed dimensions; unset ones follow the content.
    ElgOptLen min_width, ideal_width, max_width;    ///< A width range: the proposed width, clamped.
    ElgOptLen min_height, ideal_height, max_height; ///< A height range: the proposed height, clamped.
    ElgAlignment alignment;                         ///< Where the content sits in the frame.
};

/// Wraps `View::frame()`.
///
/// @param v The view (borrowed).
/// @param o The frame (borrowed).
/// @return `v` in the frame (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_frame(const View* v, const ElgFrameOptions* o) {
    ELG_TRY
    const bool flexible = o->min_width.has || o->ideal_width.has || o->max_width.has || o->min_height.has ||
                          o->ideal_height.has || o->max_height.has;
    if (!flexible) {
        return out(v->frame(FrameOptions{length(o->width), length(o->height), alignment(o->alignment)}));
    }
    // A fixed dimension in a flexible frame: the same min, ideal, and max.
    FlexibleFrameOptions f;
    f.min_width = o->width.has ? length(o->width) : length(o->min_width);
    f.ideal_width = o->width.has ? length(o->width) : length(o->ideal_width);
    f.max_width = o->width.has ? length(o->width) : length(o->max_width);
    f.min_height = o->height.has ? length(o->height) : length(o->min_height);
    f.ideal_height = o->height.has ? length(o->height) : length(o->ideal_height);
    f.max_height = o->height.has ? length(o->height) : length(o->max_height);
    f.alignment = alignment(o->alignment);
    return out(v->frame(f));
    ELG_CATCH(nullptr)
}

/// Wraps `View::frame()` with a Size prop: a fixed size that follows state, laid out again whenever the size
/// changes.
///
/// @param v The view (borrowed).
/// @param size A Size prop (ElgSize), in points.
/// @param a Where the content sits in the frame.
/// @return `v` in the frame (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_frame_size(const View* v, ElgProp size, ElgAlignment a) {
    ELG_TRY
    const Consumed consumed(size);
    return out(v->frame(prop<Size>(size), alignment(a)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::fixed_size()`: proposes nothing on the given axes, so the content takes its ideal size.
///
/// @param v The view (borrowed).
/// @param horizontal Its ideal width.
/// @param vertical Its ideal height.
/// @return `v` at its ideal size on those axes (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_fixed_size(const View* v, bool horizontal, bool vertical) {
    MOD_TRY(v->fixed_size(horizontal, vertical))
}
/// Wraps `View::aspect_ratio()`: sizes the view to an aspect ratio within what's proposed.
///
/// @param v The view (borrowed).
/// @param has_ratio false: the view's own ideal size gives the ratio (an image's).
/// @param ratio Width / height, when `has_ratio`.
/// @param mode `ContentMode`: 0 fit (all of it shows), 1 fill (it covers the space).
/// @return `v` at that aspect ratio (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_aspect_ratio(const View* v, bool has_ratio, float ratio, int32_t mode) {
    ELG_TRY
    if (has_ratio) {
        return out(v->aspect_ratio(ratio, static_cast<ContentMode>(mode)));
    }
    return out(v->aspect_ratio(static_cast<ContentMode>(mode)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::resizable()`: a raster Image takes whatever size it's offered instead of its own.
///
/// @param v A file or data Image, before other modifiers (borrowed).
/// @return The resizable image (a new view, owned by the caller), or null on failure: a UsageError for any
///   other view.
ELG_EXPORT View* elg_mod_resizable(const View* v) {
    MOD_TRY(v->resizable())
}
/// Wraps `View::layout_priority()`: stacks give space to higher priorities first. Default 0.
///
/// @param v The view (borrowed).
/// @param priority The priority.
/// @return `v` with the priority (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_layout_priority(const View* v, float priority) {
    MOD_TRY(v->layout_priority(priority))
}
/// Wraps `View::z_index()`: stacking order among siblings; higher draws in front and is hit first. Default 0.
///
/// @param v The view (borrowed).
/// @param z A float prop.
/// @return `v` at that z index (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_z_index(const View* v, ElgProp z) {
    ELG_TRY
    const Consumed consumed(z);
    return out(v->z_index(prop<float>(z)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::offset()`: moves the content when drawn, without affecting layout.
///
/// @param v The view (borrowed).
/// @param x A float prop: the horizontal move, in points.
/// @param y A float prop: the vertical move, in points.
/// @return `v`, moved (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_offset(const View* v, ElgProp x, ElgProp y) {
    ELG_TRY
    const Consumed consumed(x, y);
    return out(v->offset(prop<float>(x), prop<float>(y)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::background()` with a view.
///
/// @param v The view (borrowed).
/// @param take_background Drawn behind `v`, sized to it (consumed).
/// @param a Where the background sits.
/// @return `v` on the background (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_background_view(const View* v, View* take_background, ElgAlignment a) {
    ELG_TRY
    const Consumed consumed(take_background);
    return out(v->background(take(take_background), alignment(a)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::background()` with a color or gradient filling the view's bounds.
///
/// @param v The view (borrowed).
/// @param style A ShapeStyle prop (ElgShapeStyle).
/// @return `v` on the background (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_background(const View* v, ElgProp style) {
    ELG_TRY
    const Consumed consumed(style);
    return out(v->background(prop<ShapeStyle>(style)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::background()` with a color or gradient filling a shape.
///
/// @param v The view (borrowed).
/// @param style A ShapeStyle prop (ElgShapeStyle).
/// @param shape The shape, filling the view's bounds (borrowed).
/// @return `v` on the background (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_background_shape(const View* v, ElgProp style, const Shape* shape) {
    ELG_TRY
    const Consumed consumed(style);
    return out(v->background(prop<ShapeStyle>(style), *shape));
    ELG_CATCH(nullptr)
}
/// Wraps `View::overlay()`.
///
/// @param v The view (borrowed).
/// @param take_overlay Drawn in front of `v`, sized to it (consumed).
/// @param a Where the overlay sits.
/// @return `v` under the overlay (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_overlay(const View* v, View* take_overlay, ElgAlignment a) {
    ELG_TRY
    const Consumed consumed(take_overlay);
    return out(v->overlay(take(take_overlay), alignment(a)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::border()`: a border drawn inside the view's bounds, or along `shape`.
///
/// @param v The view (borrowed).
/// @param c A Color prop: the border's color.
/// @param width The border's width, in points.
/// @param shape The border's shape (borrowed); null: the view's bounds.
/// @return `v` with the border (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_border(const View* v, ElgProp c, float width, const Shape* shape) {
    ELG_TRY
    const Consumed consumed(c);
    if (shape) {
        return out(v->border(prop<Color>(c), width, *shape));
    }
    return out(v->border(prop<Color>(c), width));
    ELG_CATCH(nullptr)
}
/// Wraps `View::clipped()`: clips the content to the view's bounds.
///
/// @param v The view (borrowed).
/// @return `v`, clipped (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_clipped(const View* v) {
    MOD_TRY(v->clipped())
}
/// Wraps `View::clip_shape()`: clips the content to a shape filling the view's bounds.
///
/// @param v The view (borrowed).
/// @param shape The clip's shape (borrowed).
/// @return `v`, clipped (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_clip_shape(const View* v, const Shape* shape) {
    MOD_TRY(v->clip_shape(*shape))
}
/// Wraps `View::corner_radius()`: clips the content to a rounded rectangle.
///
/// @param v The view (borrowed).
/// @param radius The corners' radius, in points.
/// @return `v`, clipped (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_corner_radius(const View* v, float radius) {
    MOD_TRY(v->corner_radius(radius))
}
/// Wraps `View::opacity()`: fades the view as a group (overlapping content doesn't show through itself).
///
/// @param v The view (borrowed).
/// @param opacity A float prop: 0 transparent ... 1 opaque.
/// @return `v` at that opacity (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_opacity(const View* v, ElgProp opacity) {
    ELG_TRY
    const Consumed consumed(opacity);
    return out(v->opacity(prop<float>(opacity)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::shadow()`: a blurred shadow of the view's content.
///
/// @param v The view (borrowed).
/// @param s The shadow.
/// @return `v` with the shadow (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_shadow(const View* v, ElgShadow s) {
    MOD_TRY(v->shadow(shadow(s)))
}
/// Wraps `View::inner_shadow()`: a shadow cast inside the view's content, from its edges inward.
///
/// @param v The view (borrowed).
/// @param s The shadow.
/// @return `v` with the shadow (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_inner_shadow(const View* v, ElgShadow s) {
    MOD_TRY(v->inner_shadow(shadow(s)))
}
/// Wraps `View::blur()`: a Gaussian blur of the content.
///
/// @param v The view (borrowed).
/// @param radius A float prop: the blur's radius, in points.
/// @param opaque The content fills its bounds: edges stay sharp instead of fading out.
/// @return `v`, blurred (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_blur(const View* v, ElgProp radius, bool opaque) {
    ELG_TRY
    const Consumed consumed(radius);
    return out(v->blur(prop<float>(radius), opaque));
    ELG_CATCH(nullptr)
}
/// Wraps `View::backdrop_blur()`: blurs what's drawn behind the view in the window (frosted glass), inside
/// `shape`. Like a background, it goes behind everything the view draws.
///
/// @param v The view (borrowed).
/// @param radius A float prop: the blur's radius, in points.
/// @param shape Where the blur shows (borrowed); null: the view's bounds.
/// @param saturation A float prop: 0 gray, 1 unchanged, above 1 more vivid.
/// @param brightness A float prop: added to each channel, -1 black ... 0 unchanged ... 1 white.
/// @param noise A float prop: grain, 0...1, so smooth gradients don't band.
/// @return `v` over the blurred backdrop (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_backdrop_blur(const View* v, ElgProp radius, const Shape* shape, ElgProp saturation,
                                       ElgProp brightness, ElgProp noise) {
    ELG_TRY
    const Consumed consumed(radius, saturation, brightness, noise);
    const BackdropOptions options{prop<float>(saturation), prop<float>(brightness), prop<float>(noise)};
    if (shape) {
        return out(v->backdrop_blur(prop<float>(radius), *shape, options));
    }
    return out(v->backdrop_blur(prop<float>(radius), options));
    ELG_CATCH(nullptr)
}
/// Wraps `ColorFilterModifier`: the color adjustments `View::brightness()`, `contrast()`, `saturation()`,
/// `grayscale()`, `hue_rotation()`, `sepia()`, `color_invert()`, `color_multiply()` and `luminance_to_alpha()`.
///
/// @param v The view (borrowed).
/// @param kind `ColorFilter::Kind`: 0 brightness, 1 contrast, 2 saturation, 3 grayscale, 4 hue_rotation,
///   5 sepia, 6 invert, 7 multiply, 8 luminance_to_alpha.
/// @param amount A float prop: how much (degrees for hue_rotation); unused for multiply and
///   luminance_to_alpha.
/// @param c A Color prop: what multiply multiplies by; unused for the others.
/// @return `v`, adjusted (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_color_filter(const View* v, int32_t kind, ElgProp amount, ElgProp c) {
    ELG_TRY
    const Consumed consumed(amount, c);
    ColorFilter filter;
    filter.kind = static_cast<ColorFilter::Kind>(kind);
    filter.amount = prop<float>(amount);
    filter.color = prop<Color>(c);
    return out(v->modifier(ColorFilterModifier(std::move(filter))));
    ELG_CATCH(nullptr)
}
/// Wraps `View::blend_mode()`: how the view's pixels combine with what's behind them.
///
/// @param v The view (borrowed).
/// @param mode `BlendMode`, by its position in the enum (0 normal, 1 multiply, ...).
/// @return `v`, blended (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_blend_mode(const View* v, int32_t mode) {
    MOD_TRY(v->blend_mode(static_cast<BlendMode>(mode)))
}
/// Wraps `View::compositing_group()`: draws the content offscreen first, then composites it as one image.
///
/// @param v The view (borrowed).
/// @return `v` as a group (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_compositing_group(const View* v) {
    MOD_TRY(v->compositing_group())
}
/// Wraps `View::rotation_effect()`: rotates the drawn content clockwise about `anchor`, without affecting
/// layout or hit testing.
///
/// @param v The view (borrowed).
/// @param degrees A float prop: the rotation, in degrees.
/// @param anchor A unit point of the view's bounds; (0.5, 0.5) is the center.
/// @return `v`, rotated (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_rotation_effect(const View* v, ElgProp degrees, ElgPoint anchor) {
    ELG_TRY
    const Consumed consumed(degrees);
    return out(v->rotation_effect(prop<float>(degrees), unit_point(anchor)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::scale_effect()`: scales the drawn content about `anchor`, without affecting layout or hit
/// testing. A uniform scale stays sharp; an uneven one renders offscreen first.
///
/// @param v The view (borrowed).
/// @param x A float prop: the horizontal scale, or both when `y` is null.
/// @param y A float prop: the vertical scale (borrowed; its closure is taken over); null: a uniform scale by
///   `x`.
/// @param anchor A unit point of the view's bounds; (0.5, 0.5) is the center.
/// @return `v`, scaled (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_scale_effect(const View* v, ElgProp x, const ElgProp* y, ElgPoint anchor) {
    ELG_TRY
    ElgProp y_scale = y ? *y : ElgProp{};
    const Consumed consumed(x, y_scale);
    if (!y) {
        return out(v->scale_effect(prop<float>(x), unit_point(anchor)));
    }
    return out(v->scale_effect(prop<float>(x), prop<float>(y_scale), unit_point(anchor)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::mask()` with a view: shows the content only where `take_mask` is opaque.
///
/// @param v The view (borrowed).
/// @param take_mask The mask, sized and aligned like an overlay (consumed); only its alpha matters.
/// @param a Where the mask sits.
/// @return `v`, masked (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_mask(const View* v, View* take_mask, ElgAlignment a) {
    ELG_TRY
    const Consumed consumed(take_mask);
    return out(v->mask(take(take_mask), alignment(a)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::mask()` with a color or gradient: shows the content only where `style` is opaque.
///
/// @param v The view (borrowed).
/// @param style A ShapeStyle prop (ElgShapeStyle) filling the view's bounds; only its alpha matters.
/// @return `v`, masked (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_mask_style(const View* v, ElgProp style) {
    ELG_TRY
    const Consumed consumed(style);
    return out(v->mask(prop<ShapeStyle>(style)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::content_shape()`: hit tests against `shape`, filling the bounds, instead of the drawn content.
///
/// @param v The view (borrowed).
/// @param shape The shape (borrowed).
/// @return `v` with that hit area (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_content_shape(const View* v, const Shape* shape) {
    MOD_TRY(v->content_shape(*shape))
}
/// Wraps `View::allows_hit_testing()`.
///
/// @param v The view (borrowed).
/// @param enabled false: the view and everything in it ignore the pointer; events go to what's behind.
/// @return `v`, hit tested or not (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_allows_hit_testing(const View* v, bool enabled) {
    MOD_TRY(v->allows_hit_testing(enabled))
}
/// Wraps `View::on_click()`: clicks pressed and released inside the view. The innermost click handler wins.
///
/// @param v The view (borrowed).
/// @param on_click Runs on each click. args: ElgClickEvent*. Required: none is a usage error.
/// @param button `MouseButton`: 0 left, 1 middle, 2 right, 3 other.
/// @param count The clicks: 1 single, 2 double (the first click goes on to outer handlers), 3 triple, ...
/// @return `v` with the handler (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_on_click(const View* v, ElgClosure on_click, int32_t button, int32_t count) {
    ELG_TRY
    const Consumed consumed(on_click);
    const SharedFn f = required(on_click, "on_click");
    return out(v->on_click(
        [f](const ClickEvent& event) {
            ElgClickEvent arg = c_click_event(event);
            f->call(&arg, nullptr);
        },
        ClickOptions{static_cast<MouseButton>(button), count}));
    ELG_CATCH(nullptr)
}
/// Wraps `View::on_activate()`: the view's action, however it's used: a left click, Space or Enter while it
/// has focus, or a screen reader's default action. Makes the view focusable; disabled views don't activate.
///
/// @param v The view (borrowed).
/// @param on_activate The action. No args. Required: none is a usage error.
/// @return `v` with the action (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_on_activate(const View* v, ElgClosure on_activate) {
    ELG_TRY
    const Consumed consumed(on_activate);
    return out(v->on_activate(action(required(on_activate, "on_activate"))));
    ELG_CATCH(nullptr)
}
/// Wraps `View::on_size_change()`: called after the first layout and whenever the view's size in pixels
/// changes, including with the display scale. Runs on the UI thread, before paint.
///
/// @param v The view (borrowed).
/// @param on_size_change args: ElgPixelSize* (the logical size times the display scale, rounded up; may be
///   zero). Required: none is a usage error.
/// @return `v` with the handler (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_on_size_change(const View* v, ElgClosure on_size_change) {
    ELG_TRY
    const Consumed consumed(on_size_change);
    const SharedFn f = required(on_size_change, "on_size_change");
    return out(v->on_size_change([f](PixelSize size) {
        ElgPixelSize arg{size.width, size.height};
        f->call(&arg, nullptr);
    }));
    ELG_CATCH(nullptr)
}

// --- Gestures ---

// A callback taking a C event `C` made from the C++ event `E` by `convert`; empty if none.
template <class E, class C>
static std::function<void(const E&)> event_callback(SharedFn f, C (*convert)(const E&)) {
    if (!f) {
        return {};
    }
    return [f = std::move(f), convert](const E& event) {
        C arg = convert(event);
        f->call(&arg, nullptr);
    };
}

/// How a pointer gesture competes with others for a press (`GestureOptions`).
struct ElgGestureOptions {
    int32_t priority;  ///< `GesturePriority`: 0 normal, 1 high (offered the press before gestures inside).
    bool simultaneous; ///< Recognizes alongside whatever has the press, without taking it.
};

static GestureOptions gesture_options(ElgGestureOptions o) {
    return {static_cast<GesturePriority>(o.priority), o.simultaneous};
}

/// When a drag begins (`DragOptions`).
struct ElgDragOptions {
    float minimum_distance; ///< How far the pointer moves, in points, before the drag begins; 0: on the press.
    int32_t button;         ///< `MouseButton`: 0 left, 1 middle, 2 right, 3 other.
    double hold;            ///< Seconds the press must be held before the drag can begin; 0: no hold.
    float hold_tolerance;   ///< How far the pointer may move during the hold, in points.
};

/// Wraps `View::recognize()` with a `DragGesture`. Once recognized, the drag takes the press over: a button
/// or click handler under it doesn't fire.
///
/// @param v The view (borrowed).
/// @param o When the drag begins.
/// @param began The drag was recognized. args: ElgDragEvent*; may be none.
/// @param changed The pointer moved (also right after `began`). args: ElgDragEvent*; may be none.
/// @param ended The button was released. args: ElgDragEvent*; may be none.
/// @param cancelled The pointer was lost before the release. No args; may be none.
/// @param options How it competes with other gestures for the press.
/// @return `v` with the gesture (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_drag_gesture(const View* v, ElgDragOptions o, ElgClosure began, ElgClosure changed,
                                      ElgClosure ended, ElgClosure cancelled, ElgGestureOptions options) {
    ELG_TRY
    const Consumed consumed(began, changed, ended, cancelled);
    DragGesture gesture({o.minimum_distance, static_cast<MouseButton>(o.button), o.hold, o.hold_tolerance});
    if (auto f = event_callback(share(began), &c_drag_event)) {
        gesture = gesture.on_began(std::move(f));
    }
    if (auto f = event_callback(share(changed), &c_drag_event)) {
        gesture = gesture.on_changed(std::move(f));
    }
    if (auto f = event_callback(share(ended), &c_drag_event)) {
        gesture = gesture.on_ended(std::move(f));
    }
    if (auto f = action(share(cancelled))) {
        gesture = gesture.on_cancelled(std::move(f));
    }
    return out(v->recognize(std::move(gesture), gesture_options(options)));
    ELG_CATCH(nullptr)
}

/// When a press is long (`LongPressOptions`, without `pressing`).
struct ElgLongPressOptions {
    double duration;        ///< Seconds the press is held.
    float maximum_distance; ///< How far the pointer may wander, in points, before the press stops counting.
    int32_t button;         ///< `MouseButton`: 0 left, 1 middle, 2 right, 3 other.
};

/// Wraps `View::recognize()` with a `LongPressGesture`: runs `on_press` once a press has been held for the
/// duration. The click it would have been then doesn't happen.
///
/// @param v The view (borrowed).
/// @param on_press The action. No args.
/// @param o When a press is long.
/// @param pressing Called with true when a press that could become the long press goes down, and false when
///   it lifts, moves away or is taken by something else. args: bool*; may be none.
/// @param options How it competes with other gestures for the press.
/// @return `v` with the gesture (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_long_press_gesture(const View* v, ElgClosure on_press, ElgLongPressOptions o, ElgClosure pressing,
                                           ElgGestureOptions options) {
    ELG_TRY
    const Consumed consumed(on_press, pressing);
    LongPressOptions long_press{o.duration, o.maximum_distance, static_cast<MouseButton>(o.button),
                                consumer<bool>(share(pressing))};
    return out(v->recognize(LongPressGesture(action(share(on_press)), std::move(long_press)), gesture_options(options)));
    ELG_CATCH(nullptr)
}

template <class Event, class C>
static PhasedGesture<Event> phased_gesture(SharedFn began, SharedFn changed, SharedFn ended, SharedFn cancelled,
                                           C (*convert)(const Event&)) {
    PhasedGesture<Event> gesture;
    if (auto f = event_callback(std::move(began), convert)) {
        gesture = gesture.on_began(std::move(f));
    }
    if (auto f = event_callback(std::move(changed), convert)) {
        gesture = gesture.on_changed(std::move(f));
    }
    if (auto f = event_callback(std::move(ended), convert)) {
        gesture = gesture.on_ended(std::move(f));
    }
    if (auto f = action(std::move(cancelled))) {
        gesture = gesture.on_cancelled(std::move(f));
    }
    return gesture;
}

/// Wraps `View::recognize()` with a `MagnifyGesture`: pinching on a trackpad. The innermost one under the
/// pointer that takes the start gets the rest.
///
/// @param v The view (borrowed).
/// @param began The pinch started. args: ElgMagnifyEvent*; may be none.
/// @param changed The fingers moved. args: ElgMagnifyEvent*; may be none.
/// @param ended The pinch ended. args: ElgMagnifyEvent*; may be none.
/// @param cancelled The system took the gesture over. No args; may be none.
/// @return `v` with the gesture (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_magnify_gesture(const View* v, ElgClosure began, ElgClosure changed, ElgClosure ended,
                                         ElgClosure cancelled) {
    ELG_TRY
    const Consumed consumed(began, changed, ended, cancelled);
    return out(v->recognize(phased_gesture(share(began), share(changed), share(ended), share(cancelled), &c_magnify_event)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::recognize()` with a `RotateGesture`: turning two fingers on a trackpad. The innermost one
/// under the pointer that takes the start gets the rest.
///
/// @param v The view (borrowed).
/// @param began The turn started. args: ElgRotateEvent*; may be none.
/// @param changed The fingers moved. args: ElgRotateEvent*; may be none.
/// @param ended The turn ended. args: ElgRotateEvent*; may be none.
/// @param cancelled The system took the gesture over. No args; may be none.
/// @return `v` with the gesture (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_rotate_gesture(const View* v, ElgClosure began, ElgClosure changed, ElgClosure ended,
                                        ElgClosure cancelled) {
    ELG_TRY
    const Consumed consumed(began, changed, ended, cancelled);
    return out(v->recognize(phased_gesture(share(began), share(changed), share(ended), share(cancelled), &c_rotate_event)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::on_two_finger_double_tap()`: a two-finger double tap on a trackpad (usually: zoom to fit).
///
/// @param v The view (borrowed).
/// @param on_tap args: ElgPoint* (where, in window coordinates). Required: none is a usage error.
/// @return `v` with the gesture (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_on_two_finger_double_tap(const View* v, ElgClosure on_tap) {
    ELG_TRY
    const Consumed consumed(on_tap);
    const SharedFn f = required(on_tap, "on_tap");
    return out(v->on_two_finger_double_tap([f](Point location) {
        ElgPoint arg = c_point(location);
        f->call(&arg, nullptr);
    }));
    ELG_CATCH(nullptr)
}

// --- Pointer, scrolling, keys, text ---

/// Wraps `View::pointer_style()`: the pointer's shape over the view, like CSS's `cursor`. Views inside can set
/// their own.
///
/// @param v The view (borrowed).
/// @param style A PointerStyle prop (int32_t).
/// @return `v` with the pointer style (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_pointer_style(const View* v, ElgProp style) {
    ELG_TRY
    const Consumed consumed(style);
    return out(v->pointer_style(prop<PointerStyle>(style)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::on_pointer_enter()` and `View::on_pointer_leave()`: called at once when the pointer comes over
/// the view (its hit area), or when it leaves.
///
/// @param v The view (borrowed).
/// @param leave true: when the pointer leaves (on_pointer_leave); false: when it comes over (on_pointer_enter).
/// @param on_cross The action. No args.
/// @return `v` with the handler (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_on_pointer_boundary(const View* v, bool leave, ElgClosure on_cross) {
    ELG_TRY
    const Consumed consumed(on_cross);
    if (leave) {
        return out(v->on_pointer_leave(action(share(on_cross))));
    }
    return out(v->on_pointer_enter(action(share(on_cross))));
    ELG_CATCH(nullptr)
}
/// Wraps `View::on_hover_change()`: hover intent. Called with true once the pointer has rested on the view for
/// the hover delay, and false when it leaves (after the exit delay).
///
/// @param v The view (borrowed).
/// @param on_hover args: bool*.
/// @return `v` with the handler (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_on_hover_change(const View* v, ElgClosure on_hover) {
    ELG_TRY
    const Consumed consumed(on_hover);
    return out(v->on_hover_change(consumer<bool>(share(on_hover))));
    ELG_CATCH(nullptr)
}
/// Wraps `View::on_pointer()`: raw pointer events over the view. Consuming a down captures the pointer, so the
/// moves and the up come here wherever it goes.
///
/// @param v The view (borrowed).
/// @param on_pointer args: ElgPointerEvent* (in the view's coordinates), ret: bool* (true consumes it).
///   Required: none is a usage error.
/// @return `v` with the handler (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_on_pointer(const View* v, ElgClosure on_pointer) {
    ELG_TRY
    const Consumed consumed(on_pointer);
    const SharedFn f = required(on_pointer, "on_pointer");
    return out(v->on_pointer([f](const PointerEvent& event) {
        ElgPointerEvent arg = c_pointer_event(event);
        bool consumed = false;
        f->call(&arg, &consumed);
        return consumed;
    }));
    ELG_CATCH(nullptr)
}
/// Wraps `View::on_scroll()`: wheel and trackpad scrolling over the view.
///
/// @param v The view (borrowed).
/// @param on_scroll args: ElgScrollEvent* (in the view's coordinates), ret: bool* (true consumes it: scroll
///   views around it then don't scroll). Required: none is a usage error.
/// @return `v` with the handler (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_on_scroll(const View* v, ElgClosure on_scroll) {
    ELG_TRY
    const Consumed consumed(on_scroll);
    const SharedFn f = required(on_scroll, "on_scroll");
    return out(v->on_scroll([f](const ScrollEvent& event) {
        ElgScrollEvent arg = c_scroll_event(event);
        bool consumed = false;
        f->call(&arg, &consumed);
        return consumed;
    }));
    ELG_CATCH(nullptr)
}
/// Wraps `View::on_text_input()`: makes the view focusable and sees the text typed while it has focus: commits,
/// and an IME's compositions in progress. Keys still go to elg_mod_on_key().
///
/// @param v The view (borrowed).
/// @param on_text_input args: ElgTextInputEvent* (its text valid during the call), ret: bool* (true: handled).
///   Required: none is a usage error.
/// @return `v` with the handler (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_on_text_input(const View* v, ElgClosure on_text_input) {
    ELG_TRY
    const Consumed consumed(on_text_input);
    const SharedFn f = required(on_text_input, "on_text_input");
    return out(v->on_text_input([f](const TextInputEvent& event) {
        ElgTextInputEvent arg{static_cast<int32_t>(event.kind), c_str(event.text), event.cursor};
        bool consumed = false;
        f->call(&arg, &consumed);
        return consumed;
    }));
    ELG_CATCH(nullptr)
}

/// Files dropped on a view: what elg_mod_on_drop()'s `files` gets.
struct ElgDropFiles {
    const ElgStr* paths; ///< Their paths, in UTF-8, valid during the call.
    size_t count;        ///< How many `paths` there are.
    ElgPoint location;   ///< Where they were dropped, in the view's coordinates.
};

/// Text dropped on a view: what elg_mod_on_drop()'s `text` gets.
struct ElgDropText {
    ElgStr text;       ///< The text, valid during the call.
    ElgPoint location; ///< Where it was dropped, in the view's coordinates.
};

/// Wraps `View::on_drop()`: files or text dragged from other apps can be dropped here. The innermost view under
/// the pointer that takes that kind of thing gets it.
///
/// @param v The view (borrowed).
/// @param files Takes dropped files. args: ElgDropFiles*; ret: a bool* that's ignored (what it returns has no
///   effect). None: files aren't taken.
/// @param text Takes dropped text. args: ElgDropText*; ret: a bool* that's ignored. None: text isn't taken.
/// @param targeted Called with true while a drag the view can take is over it (for a highlight), and false
///   when it leaves. args: bool*; may be none.
/// @return `v` taking drops (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_on_drop(const View* v, ElgClosure files, ElgClosure text, ElgClosure targeted) {
    ELG_TRY
    const Consumed consumed(files, text, targeted);
    DropOptions options;
    if (const SharedFn f = share(files)) {
        options.files = [f](const std::vector<std::filesystem::path>& paths, Point location) {
            std::vector<std::string> names;
            names.reserve(paths.size());
            for (const auto& p : paths) {
                names.push_back(utf8(p));
            }
            std::vector<ElgStr> c_names;
            c_names.reserve(names.size());
            for (const auto& n : names) {
                c_names.push_back(c_str(n));
            }
            ElgDropFiles arg{c_names.data(), c_names.size(), {location.x, location.y}};
            bool ignored = false; // bindings that still return a result write it here
            f->call(&arg, &ignored);
        };
    }
    if (const SharedFn f = share(text)) {
        options.text = [f](const std::string& dropped, Point location) {
            ElgDropText arg{c_str(dropped), {location.x, location.y}};
            bool ignored = false; // bindings that still return a result write it here
            f->call(&arg, &ignored);
        };
    }
    options.targeted = consumer<bool>(share(targeted));
    return out(v->on_drop(std::move(options)));
    ELG_CATCH(nullptr)
}

/// Wraps `View::accessibility_label()`: what the view is called, for screen readers.
///
/// @param v The view (borrowed).
/// @param label A string prop.
/// @return `v` with the label (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_accessibility_label(const View* v, ElgProp label) {
    ELG_TRY
    const Consumed consumed(label);
    return out(v->accessibility_label(prop<std::string>(label)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::accessibility_value()`: what the view holds, as words ("3 of 10", "50%").
///
/// @param v The view (borrowed).
/// @param value A string prop.
/// @return `v` with the value (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_accessibility_value(const View* v, ElgProp value) {
    ELG_TRY
    const Consumed consumed(value);
    return out(v->accessibility_value(prop<std::string>(value)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::accessibility_hint()`: what happens if the view is used, when the label doesn't say.
///
/// @param v The view (borrowed).
/// @param hint A string prop.
/// @return `v` with the hint (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_accessibility_hint(const View* v, ElgProp hint) {
    ELG_TRY
    const Consumed consumed(hint);
    return out(v->accessibility_hint(prop<std::string>(hint)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::accessibility_hidden()`: leaves the view, and everything inside, out for screen readers
/// (decoration).
///
/// @param v The view (borrowed).
/// @param hidden Whether it's left out.
/// @return `v`, hidden or not (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_accessibility_hidden(const View* v, bool hidden) {
    MOD_TRY(v->accessibility_hidden(hidden))
}
/// Wraps `View::accessibility_role()`: what the view is to screen readers, when the automatic choice isn't
/// right.
///
/// @param v The view (borrowed).
/// @param role `AccessibilityRole`, by its position in the enum (0 group, 1 label, 2 heading, 3 button, ...).
/// @return `v` with the role (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_accessibility_role(const View* v, int32_t role) {
    MOD_TRY(v->accessibility_role(static_cast<AccessibilityRole>(role)))
}
/// Wraps `View::accessibility_element()`: one element for everything inside, labeled with their text.
///
/// @param v The view (borrowed).
/// @return `v` as one element (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_accessibility_element(const View* v) {
    MOD_TRY(v->accessibility_element())
}
/// Wraps `View::accessibility_action()`: what a screen reader's default action does, for views made clickable
/// with gestures.
///
/// @param v The view (borrowed).
/// @param on_action The action. No args. Required: none is a usage error.
/// @return `v` with the action (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_accessibility_action(const View* v, ElgClosure on_action) {
    ELG_TRY
    const Consumed consumed(on_action);
    return out(v->accessibility_action(action(required(on_action, "on_action"))));
    ELG_CATCH(nullptr)
}
/// Wraps `View::hover_delay()`: the hover delay for on_hover_change handlers and submenus inside (default:
/// enter 0.1 s, exit 0).
///
/// @param v The view (borrowed).
/// @param delay The delays, in seconds.
/// @return `v` with the delay (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_hover_delay(const View* v, ElgHoverDelay delay) {
    MOD_TRY(v->hover_delay(hover_delay(delay)))
}
/// Wraps `View::window_drag_area()`: pressing and dragging here moves the window (custom title bars), and
/// double-clicking maximizes it. Controls inside still work.
///
/// @param v The view (borrowed).
/// @param enabled Whether it moves the window.
/// @return `v` as a drag area or not (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_window_drag_area(const View* v, bool enabled) {
    MOD_TRY(v->window_drag_area(enabled))
}
/// Wraps `View::context_menu()`: a right-click (or Shift+F10 or the Menu key while focused) opens the menu
/// `menu` builds. Item actions run after it closes.
///
/// @param v The view (borrowed).
/// @param menu Builds the menu each time it opens, usually with elg_menu(). ret: View*. Required: none is a
///   usage error.
/// @return `v` with the menu (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_context_menu(const View* v, ElgClosure menu) {
    ELG_TRY
    const Consumed consumed(menu);
    return out(v->context_menu(view_builder(required(menu, "menu"))));
    ELG_CATCH(nullptr)
}
/// The options of elg_mod_floating(): `FloatingOptions`.
struct ElgFloatingOptions {
    ElgFloatingPlacement placement; ///< Where the content goes, by the view.
    int32_t boundary;               ///< `FloatingBoundary`: 0 screen, 1 window, 2 container.
    bool outside_click;             ///< A press anywhere outside closes it (and only closes it).
    bool escape;                    ///< Esc closes it, unless something focused handles it first.
    ElgPopupMotion motion;          ///< How it opens and closes; `has` false: at once.
};

/// Wraps `View::floating()`: `content` floating above everything by the view while `take_is_presented` is
/// true, with no chrome of its own.
///
/// @param v The view (borrowed).
/// @param take_is_presented Whether it's shown (consumed); set back to false when it's dismissed.
/// @param content Builds the content each time it opens. ret: View*. Required: none is a usage error.
/// @param o The options (borrowed; required).
/// @return `v` with the floating content (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_floating(const View* v, Binding<bool>* take_is_presented, ElgClosure content,
                                  const ElgFloatingOptions* o) {
    ELG_TRY
    const Consumed consumed(take_is_presented, content);
    const std::unique_ptr<Binding<bool>> is_presented = own(take_is_presented);
    FloatingOptions options;
    options.placement = placement(o->placement);
    options.boundary = static_cast<FloatingBoundary>(o->boundary);
    options.dismiss = {o->outside_click, o->escape};
    options.motion = motion(o->motion);
    return out(v->floating(*is_presented, view_builder(required(content, "content")), std::move(options)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::floating_container()`: marks the view as where `FloatingBoundary::container` content inside it
/// stays.
///
/// @param v The view (borrowed).
/// @return `v` as a container (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_floating_container(const View* v) {
    MOD_TRY(v->floating_container())
}

/// The options of elg_mod_tooltip(): `TooltipOptions`.
struct ElgTooltipOptions {
    ElgFloatingPlacement placement; ///< Where the tooltip goes, by the view.
    int32_t boundary;               ///< `FloatingBoundary`: 0 screen, 1 window, 2 container.
    double delay;                   ///< Seconds the pointer rests on the view before it shows.
    ElgPopupMotion motion;          ///< How it opens and closes; `has` false: the TooltipStyle's.
};

static TooltipOptions tooltip_options(const ElgTooltipOptions& o) {
    return {placement(o.placement), static_cast<FloatingBoundary>(o.boundary), o.delay, motion(o.motion)};
}

/// Wraps `View::tooltip()`: shown in the TooltipStyle after the pointer rests on the view for a moment, until
/// it leaves or presses. Screen readers don't see it.
///
/// @param v The view (borrowed).
/// @param content Builds the tooltip, when `text` is null (required then). ret: View*. Dropped unused
///   when `text` is given.
/// @param text A string prop for a text tooltip (borrowed; its closure is taken over); null: `content`.
/// @param o The options (borrowed; required).
/// @return `v` with the tooltip (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_tooltip(const View* v, ElgClosure content, const ElgProp* text, const ElgTooltipOptions* o) {
    ELG_TRY
    ElgProp title = text ? *text : ElgProp{};
    const Consumed consumed(content, title);
    if (text) {
        return out(v->tooltip(prop<std::string>(title), tooltip_options(*o)));
    }
    return out(v->tooltip(view_builder(required(content, "content")), tooltip_options(*o)));
    ELG_CATCH(nullptr)
}

/// Wraps `View::popover()`: a panel of `content` beside the view while `take_is_presented` is true. Clicking
/// outside it, Esc, or switching apps closes it and sets the binding back to false.
///
/// @param v The view (borrowed).
/// @param take_is_presented Whether it's shown (consumed).
/// @param content Builds the content each time it opens. ret: View*. Required: none is a usage error.
/// @param popover_placement Where the panel goes, by the view (borrowed; required).
/// @param boundary `FloatingBoundary`: 0 screen, 1 window, 2 container.
/// @param padded A bool prop: false lets the content reach the panel's edges.
/// @param popup_motion How it opens and closes; `has` false: the PopoverStyle's.
/// @return `v` with the popover (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_popover(const View* v, Binding<bool>* take_is_presented, ElgClosure content,
                                 const ElgFloatingPlacement* popover_placement, int32_t boundary, ElgProp padded,
                                 ElgPopupMotion popup_motion) {
    ELG_TRY
    const Consumed consumed(take_is_presented, content, padded);
    const std::unique_ptr<Binding<bool>> is_presented = own(take_is_presented);
    PopoverOptions options;
    options.placement = placement(*popover_placement);
    options.boundary = static_cast<FloatingBoundary>(boundary);
    options.padded = prop<bool>(padded);
    options.motion = motion(popup_motion);
    return out(v->popover(*is_presented, view_builder(required(content, "content")), options));
    ELG_CATCH(nullptr)
}
/// Wraps `View::focusable()`.
///
/// @param v The view (borrowed).
/// @param focusable true: the view takes keyboard focus (Tab order, clicks). false: the view and everything
///   inside never take focus.
/// @return `v`, focusable or not (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_focusable(const View* v, bool focusable) {
    MOD_TRY(v->focusable(focusable))
}
/// Wraps `View::focused()`: two-way focus. Makes the view focusable.
///
/// @param v The view (borrowed).
/// @param take_is_focused True while the view has focus; setting it true focuses the view, false removes
///   focus from it (consumed).
/// @return `v` with the binding (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_focused(const View* v, Binding<bool>* take_is_focused) {
    ELG_TRY
    const Consumed consumed(take_is_focused);
    const std::unique_ptr<Binding<bool>> is_focused = own(take_is_focused);
    return out(v->focused(*is_focused));
    ELG_CATCH(nullptr)
}
/// Wraps `View::on_key_press()`: runs `on_press` when `key` is pressed while the view, or something inside, has
/// focus.
///
/// @param v The view (borrowed).
/// @param key `Key`, by its position in the enum.
/// @param on_press The action. No args.
/// @return `v` with the handler (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_on_key_press(const View* v, int32_t key, ElgClosure on_press) {
    ELG_TRY
    const Consumed consumed(on_press);
    return out(v->on_key_press(static_cast<Key>(key), action(share(on_press))));
    ELG_CATCH(nullptr)
}
/// Wraps `View::on_key()`: sees every key down and up while the view, or something inside, has focus.
/// Unhandled keys bubble out.
///
/// @param v The view (borrowed).
/// @param on_key args: ElgKeyEvent*, ret: bool* (true: handled).
/// @return `v` with the handler (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_on_key(const View* v, ElgClosure on_key) {
    ELG_TRY
    const Consumed consumed(on_key);
    return out(v->on_key(key_handler(share(on_key))));
    ELG_CATCH(nullptr)
}
// --- Selection ---

/// A ForEach key (`ItemKey`), for selections.
struct ElgItemKey {
    bool is_string;    ///< The key is `string_key`; otherwise `int_key`.
    int64_t int_key;   ///< The key, when it isn't a string.
    ElgStr string_key; ///< The key, when it's a string.
};

/// Selected keys: what elg_mod_selection()'s `set` gets.
struct ElgItemKeys {
    const ElgItemKey* keys; ///< The keys, valid during the call.
    size_t count;           ///< How many `keys` there are.
};

/// Appends a key to the result of elg_mod_selection()'s `get`.
///
/// @param target The closure's `ret`.
/// @param key The key (its string is copied).
ELG_EXPORT void elg_item_keys_push(std::vector<ItemKey>* target, ElgItemKey key) {
    ELG_TRY
    target->push_back(key.is_string ? ItemKey(str(key.string_key)) : ItemKey(key.int_key));
    ELG_CATCH_VOID
}

namespace {

ElgItemKey c_item_key(const ItemKey& key) {
    if (const auto* s = std::get_if<std::string>(&key)) {
        return {true, 0, c_str(*s)};
    }
    return {false, std::get<std::int64_t>(key), {}};
}

std::vector<ItemKey> read_keys(const RustFn& get) {
    std::vector<ItemKey> keys;
    get.call(nullptr, &keys);
    return keys;
}

void write_keys(const RustFn& set, const std::vector<ItemKey>& keys) {
    std::vector<ElgItemKey> items;
    items.reserve(keys.size());
    for (const ItemKey& key : keys) {
        items.push_back(c_item_key(key));
    }
    ElgItemKeys arg{items.data(), items.size()};
    set.call(&arg, nullptr);
}

// View::selection over the Rust selection, with keys of type K (std::int64_t or std::string).
template <class K>
View with_selection(const View& v, bool multiple, const SharedFn& get, const SharedFn& set, const SharedFn& activate) {
    SelectionOptions<K> options;
    if (activate) {
        options.on_activate = [activate](const K& key) {
            const ItemKey item = detail::to_item_key(key);
            ElgItemKey arg = c_item_key(item);
            activate->call(&arg, nullptr);
        };
    }
    if (multiple) {
        const Binding<std::set<K>> binding(
            [get] {
                std::set<K> keys;
                for (const ItemKey& key : read_keys(*get)) {
                    keys.insert(detail::from_item_key<K>(key));
                }
                return keys;
            },
            [set](const std::set<K>& keys) {
                std::vector<ItemKey> items;
                items.reserve(keys.size());
                for (const K& key : keys) {
                    items.push_back(detail::to_item_key(key));
                }
                write_keys(*set, items);
            });
        return v.selection(std::move(binding), std::move(options));
    }
    const Binding<std::optional<K>> binding(
        [get]() -> std::optional<K> {
            const std::vector<ItemKey> keys = read_keys(*get);
            return keys.empty() ? std::nullopt : std::optional<K>(detail::from_item_key<K>(keys.front()));
        },
        [set](std::optional<K> key) {
            write_keys(*set, key ? std::vector<ItemKey>{detail::to_item_key(*key)} : std::vector<ItemKey>{});
        });
    return v.selection(std::move(binding), std::move(options));
}

} // namespace

/// Wraps `View::selection()`: makes the rows of a stack over a ForEach selectable, by their ForEach keys. Put it
/// on the stack itself. Each row is drawn by the RowStyle.
///
/// @param v The stack (borrowed).
/// @param multiple A set of rows (Ctrl+click or Cmd+click, Shift ranges, select all); false: one row or none.
/// @param string_keys The ForEach's keys are strings; false: integers.
/// @param get The selected keys; a tracked read. ret: std::vector<ItemKey>* (filled with elg_item_keys_push).
///   Required: none is a usage error.
/// @param set Sets the selected keys; with `multiple` false, one key or none. args: ElgItemKeys*. Required:
///   none is a usage error.
/// @param activate Enter, or a double click on a row: opens what the row stands for. args: ElgItemKey* (its
///   string valid during the call); may be none.
/// @return The selectable stack (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_selection(const View* v, bool multiple, bool string_keys, ElgClosure get, ElgClosure set,
                                   ElgClosure activate) {
    ELG_TRY
    const Consumed consumed(get, set, activate);
    const SharedFn g = required(get, "get");
    const SharedFn s = required(set, "set");
    const SharedFn a = share(activate);
    if (string_keys) {
        return out(with_selection<std::string>(*v, multiple, g, s, a));
    }
    return out(with_selection<std::int64_t>(*v, multiple, g, s, a));
    ELG_CATCH(nullptr)
}

/// Wraps `View::font_weight()`: the weight of the font inside, keeping its size and family.
///
/// @param v The view (borrowed).
/// @param weight A Weight prop (int32_t, 1...1000).
/// @return `v` with the weight set inside (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_font_weight(const View* v, ElgProp weight) {
    ELG_TRY
    const Consumed consumed(weight);
    return out(v->font_weight(prop<Weight>(weight)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::font()`: the font of the text inside.
///
/// @param v The view (borrowed).
/// @param f A Font prop (ElgFont).
/// @return `v` with the font set inside (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_font(const View* v, ElgProp f) {
    ELG_TRY
    const Consumed consumed(f);
    return out(v->font(prop<Font>(f)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::foreground_style()`: the color of text and unfilled shapes inside.
///
/// @param v The view (borrowed).
/// @param c A Color prop.
/// @return `v` with the color set inside (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_foreground_style(const View* v, ElgProp c) {
    ELG_TRY
    const Consumed consumed(c);
    return out(v->foreground_style(prop<Color>(c)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::tint()`: the accent color of controls inside.
///
/// @param v The view (borrowed).
/// @param c A Color prop.
/// @return `v` with the tint set inside (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_tint(const View* v, ElgProp c) {
    ELG_TRY
    const Consumed consumed(c);
    return out(v->tint(prop<Color>(c)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::color_scheme()`: light or dark, for ThemeColor tokens inside.
///
/// @param v The view (borrowed).
/// @param scheme A ColorScheme prop (int32_t: 0 light, 1 dark).
/// @return `v` with the scheme set inside (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_color_scheme(const View* v, ElgProp scheme) {
    ELG_TRY
    const Consumed consumed(scheme);
    return out(v->color_scheme(prop<ColorScheme>(scheme)));
    ELG_CATCH(nullptr)
}
/// Wraps `View::theme()`: the theme inside.
///
/// @param v The view (borrowed).
/// @param theme The theme (borrowed; copied).
/// @return `v` with the theme set inside (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_theme(const View* v, const Theme* theme) {
    MOD_TRY(v->theme(*theme))
}
/// Wraps `View::disabled()`: controls inside stop responding and look disabled.
///
/// @param v The view (borrowed).
/// @param disabled A bool prop.
/// @return `v`, disabled or not (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_disabled(const View* v, ElgProp disabled) {
    ELG_TRY
    const Consumed consumed(disabled);
    return out(v->disabled(prop<bool>(disabled)));
    ELG_CATCH(nullptr)
}
namespace {
// View::animation's source: Rust returns a version number that changes when its value does.
struct VersionSource {
    SharedFn f;
    std::uint64_t operator()() const {
        std::uint64_t version = 0;
        f->call(nullptr, &version);
        return version;
    }
};
} // namespace

/// Wraps `View::animation()`: changes inside `v` animate with `animation` whenever `version` changed in the
/// same update.
///
/// @param v The view (borrowed).
/// @param animation The animation (borrowed; copied).
/// @param version A number that changes when the value the animation follows does; read as View::animation
///   reads its value. ret: uint64_t*. Required: none is a usage error.
/// @return `v`, animated (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_animation(const View* v, const Animation* animation, ElgClosure version) {
    ELG_TRY
    const Consumed consumed(version);
    return out(v->animation(*animation, VersionSource{required(version, "version")}));
    ELG_CATCH(nullptr)
}
/// Wraps `View::transition()`: how this view enters and leaves when an If or ForEach adds or removes it with
/// an animation.
///
/// @param v The view (borrowed).
/// @param transition The transition (borrowed; copied).
/// @return `v` with the transition (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_transition(const View* v, const Transition* transition) {
    MOD_TRY(v->transition(*transition))
}
/// Wraps `View::animates_layout()`.
///
/// @param v The view (borrowed).
/// @param enabled false: this view and everything inside snap to new positions and sizes instead of easing
///   there. Colors, opacity and transitions still animate.
/// @return `v`, animating layout or not (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_animates_layout(const View* v, bool enabled) {
    MOD_TRY(v->animates_layout(enabled))
}

// --- Hot reload ---

/// Wraps `HotReload()`: the views module's content, rebuilt and reloaded when its sources change; a status
/// badge shows building, errors and reloads. The module is loaded right away.
///
/// @param module The views module as the build writes it (it's loaded from a copy).
/// @param build_command Run through the shell when a watched file changes; empty: never rebuild, only reload
///   when the module file itself changes.
/// @param watch Files and folders (searched recursively) whose changes trigger a rebuild.
/// @param watch_count How many `watch` paths there are.
/// @param show `HotReloadShow`: 0 the module's root view, 1 its previews.
/// @return The view, owned by the caller, or null on failure.
ELG_EXPORT View* elg_hot_reload(ElgStr module, ElgStr build_command, const ElgStr* watch, size_t watch_count, int32_t show) {
    ELG_TRY
    HotReloadOptions options;
    options.module = path(module);
    options.build_command = str(build_command);
    for (size_t i = 0; i < watch_count; ++i) {
        options.watch.push_back(path(watch[i]));
    }
    options.show = static_cast<HotReloadShow>(show);
    return out(HotReload(std::move(options)));
    ELG_CATCH(nullptr)
}

// --- Views modules (hot reload and previews) ---
//
// A Rust views module exports elg_module_entry (elegance's `preview` feature), which calls this. The entry
// holds plain `View (*)()` functions, so each preview gets a thunk from a fixed table that calls
// back into Rust with its index (-1: the root view).

/// One preview of a Rust views module, for elg_rust_module_entry() (`PreviewEntry`, without its body).
struct ElgPreviewDesc {
    ElgStr name;  ///< The preview's name (copied).
    ElgStr file;  ///< The source file it's in (copied).
    int32_t line; ///< Its line in `file`.
};

/// Builds view `index` into `ret` (-1: the root view; otherwise a preview); false if it panicked.
using ElgModuleBuild = bool (*)(int32_t index, View* ret);

namespace {

constexpr std::size_t kMaxPreviews = 256;

// One registered module: its strings, previews, and entry. Kept for the rest of the process: the
// host holds each module's entry, and may still read it after a newer module registered.
struct RustModule {
    std::vector<std::string> strings; // stable c_str()s: reserved up front
    std::vector<PreviewEntry> previews;
    ModuleEntry entry;
};

std::vector<std::unique_ptr<RustModule>> g_modules;
// The newest module's: the views the host mounts after loading it.
ElgModuleBuild g_module_build = nullptr;

View build_module_view(int32_t index) {
    View result;
    if (!g_module_build(index, &result)) {
        throw RustPanic();
    }
    return result;
}

View module_root() {
    return build_module_view(-1);
}

template <std::size_t... I>
std::array<View (*)(), sizeof...(I)> make_preview_thunks(std::index_sequence<I...>) {
    return {+[]() -> View { return build_module_view(static_cast<int32_t>(I)); }...};
}

const std::array<View (*)(), kMaxPreviews> g_preview_thunks = make_preview_thunks(std::make_index_sequence<kMaxPreviews>{});

} // namespace

/// The `ModuleEntry` of a Rust views module: its elg_module_entry (elegance's `preview` feature) returns this.
/// Called by each module as it loads (every reload is a new module), on the UI thread.
///
/// @param has_root The module has a root view.
/// @param previews The module's previews (borrowed; their strings are copied).
/// @param count How many `previews` there are; at most 256.
/// @param build Builds the root view or a preview, by index. The newest module's is the one called.
/// @return The entry, kept by the shim for the rest of the process; null, with a UsageError, when the module
///   has more previews than the thunk table holds (also printed to stderr).
ELG_EXPORT const ModuleEntry* elg_rust_module_entry(bool has_root, const ElgPreviewDesc* previews, size_t count,
                                                    ElgModuleBuild build) {
    ELG_TRY
    if (count > kMaxPreviews) {
        // The host calls this through the module, not through the C API: say why on stderr too.
        const std::string message =
            std::format("a views module can have at most {} previews; this one has {}", kMaxPreviews, count);
        std::println(stderr, "elegance: {}", message);
        throw elg::UsageError(message);
    }
    auto module = std::make_unique<RustModule>();
    module->strings.reserve(count * 2);
    module->previews.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const char* name = module->strings.emplace_back(str(previews[i].name)).c_str();
        const char* file = module->strings.emplace_back(str(previews[i].file)).c_str();
        module->previews.push_back({name, g_preview_thunks[i], file, previews[i].line});
    }
    module->entry = {module_abi, has_root ? &module_root : nullptr, &module->previews};
    g_module_build = build;
    return &g_modules.emplace_back(std::move(module))->entry;
    ELG_CATCH(nullptr)
}

// --- Grids ---

/// How a VGrid column or HGrid row is sized (`GridItem`).
struct ElgGridItem {
    int32_t kind;           ///< `GridItem::Kind`: 0 fixed (exactly `minimum` points), 1 flexible, 2 adaptive.
    float minimum;          ///< The least a track takes, in points.
    float maximum;          ///< The most a flexible or adaptive track takes, in points.
    ElgOptLen spacing;      ///< After this track (between its tracks too, if adaptive); unset: the grid's.
    bool has_alignment;     ///< `alignment` is set; otherwise the grid's.
    ElgAlignment alignment; ///< Where views sit in this track's cells.
};

/// One resolved track (`GridTrack`): what elg_resolve_grid_tracks()'s `each` gets.
struct ElgGridTrack {
    float size;             ///< The track's size, in points.
    float spacing;          ///< The space after it, in points.
    bool has_alignment;     ///< `alignment` is set (its item's).
    ElgAlignment alignment; ///< Where views sit in its cells.
};

/// A VGrid or HGrid (kind 0 or 1: tracks `items`, `spacing_a` between lines) or a Grid (kind 2: `spacing_a`
/// between columns, `spacing_b` between rows).
struct ElgGridDesc {
    int32_t kind;             ///< 0 VGrid, 1 HGrid, anything else Grid.
    const ElgGridItem* items; ///< A VGrid's columns or an HGrid's rows (borrowed).
    size_t item_count;        ///< How many `items` there are.
    ElgAlignment alignment;   ///< Of views in their cells (and of a VGrid's or HGrid's tracks).
    /// Between a VGrid's rows or an HGrid's columns (and tracks without their own), or a Grid's columns; unset:
    /// `default_stack_spacing`.
    ElgOptLen spacing_a;
    ElgOptLen spacing_b; ///< Between a Grid's rows (unused by VGrid and HGrid); unset: `default_stack_spacing`.
};

static std::vector<GridItem> grid_items(const ElgGridItem* items, size_t count) {
    std::vector<GridItem> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        GridItem item{static_cast<GridItem::Kind>(items[i].kind), items[i].minimum, items[i].maximum, length(items[i].spacing)};
        if (items[i].has_alignment) {
            item.alignment = alignment(items[i].alignment);
        }
        result.push_back(item);
    }
    return result;
}

static std::shared_ptr<const Layout> grid_layout(const ElgGridDesc& d) {
    switch (d.kind) {
    case 0: return std::make_shared<const VGridLayout>(grid_items(d.items, d.item_count), alignment(d.alignment), length(d.spacing_a));
    case 1: return std::make_shared<const HGridLayout>(grid_items(d.items, d.item_count), alignment(d.alignment), length(d.spacing_a));
    default: return std::make_shared<const GridLayout>(alignment(d.alignment), length(d.spacing_a), length(d.spacing_b));
    }
}

/// Wraps `VGrid()`, `HGrid()` and `Grid()`: children in columns row after row, in rows column after column, or a
/// table of GridRows.
///
/// @param desc The grid (borrowed).
/// @param children The children (consumed).
/// @param count How many `children` there are.
/// @return The grid, owned by the caller, or null on failure.
ELG_EXPORT View* elg_grid(const ElgGridDesc* desc, View** children, size_t count) {
    ELG_TRY
    ViewArray array{children, count};
    const Consumed consumed(array);
    std::vector<View> views = take_all(array);
    switch (desc->kind) {
    case 0: return out(VGridLayout(grid_items(desc->items, desc->item_count), alignment(desc->alignment), length(desc->spacing_a))(std::move(views)));
    case 1: return out(HGridLayout(grid_items(desc->items, desc->item_count), alignment(desc->alignment), length(desc->spacing_a))(std::move(views)));
    default: return out(GridLayout(alignment(desc->alignment), length(desc->spacing_a), length(desc->spacing_b))(std::move(views)));
    }
    ELG_CATCH(nullptr)
}

/// Wraps `size_that_fits()` of `VGridLayout`, `HGridLayout` or `GridLayout`, for custom layouts that measure
/// their children as a grid does.
///
/// @param desc The grid (borrowed).
/// @param proposal The size offered to the grid.
/// @param children The custom layout's children (borrowed).
/// @param count How many `children` there are.
/// @return The size the grid takes; zero on failure.
ELG_EXPORT ElgSize elg_grid_layout_size(const ElgGridDesc* desc, ElgProposedSize proposal, const LayoutChild* children, size_t count) {
    ELG_TRY
    const Size s = grid_layout(*desc)->size_that_fits(proposed(proposal), std::span<const LayoutChild>(children, count));
    return {s.width, s.height};
    ELG_CATCH(ElgSize{})
}

/// Wraps `place_children()` of `VGridLayout`, `HGridLayout` or `GridLayout`, for custom layouts that place
/// their children as a grid does.
///
/// @param desc The grid (borrowed).
/// @param bounds Where the grid is, at the size elg_grid_layout_size() reported for `proposal`.
/// @param proposal The size offered to the grid.
/// @param children The custom layout's children (borrowed).
/// @param count How many `children` there are.
ELG_EXPORT void elg_grid_layout_place(const ElgGridDesc* desc, ElgRect bounds, ElgProposedSize proposal,
                                      const LayoutChild* children, size_t count) {
    ELG_TRY
    grid_layout(*desc)->place_children(rect(bounds), proposed(proposal), std::span<const LayoutChild>(children, count));
    ELG_CATCH_VOID
}

/// Wraps `GridRow()`: one row of a Grid; each child is a cell.
///
/// @param row_alignment How this row's cells line up (`VerticalAlignment`); negative: the grid's.
/// @param cells The cells (consumed).
/// @param count How many `cells` there are.
/// @return The row, owned by the caller, or null on failure.
ELG_EXPORT View* elg_grid_row(int32_t row_alignment, View** cells, size_t count) {
    ELG_TRY
    ViewArray views{cells, count};
    const Consumed consumed(views);
    GridRowOptions options;
    if (row_alignment >= 0) {
        options.alignment = static_cast<VerticalAlignment>(row_alignment);
    }
    return out(detail::make_grid_row(options, take_all(views)));
    ELG_CATCH(nullptr)
}

/// Wraps `resolve_grid_tracks()`: the tracks `items` make in a length. Fixed tracks take their size; the rest
/// share what's left equally, within their limits; an adaptive item becomes as many tracks as fit in its share.
/// At least one track results.
///
/// @param items The items (borrowed).
/// @param count How many `items` there are.
/// @param has_length false: the tracks take their ideal sizes.
/// @param length_value The length to fill, in points, when `has_length`; infinity: ideal sizes.
/// @param spacing Between items without their own spacing; unset: `default_stack_spacing`.
/// @param each Called once per track, in order, during this call. args: ElgGridTrack*. Required: none is a
///   usage error.
ELG_EXPORT void elg_resolve_grid_tracks(const ElgGridItem* items, size_t count, bool has_length, float length_value,
                                        ElgOptLen spacing, ElgClosure each) {
    ELG_TRY
    const Consumed consumed(each);
    const SharedFn f = required(each, "each");
    const std::vector<GridItem> list = grid_items(items, count);
    const std::optional<float> span = has_length ? std::optional<float>(length_value) : std::nullopt;
    for (const GridTrack& track : resolve_grid_tracks(list, span, length(spacing))) {
        ElgGridTrack arg{track.size, track.spacing, track.alignment.has_value(), c_alignment(track.alignment.value_or(Alignment{}))};
        f->call(&arg, nullptr);
    }
    ELG_CATCH_VOID
}

/// Wraps `View::grid_cell_columns()`: a Grid cell that spans `count` columns.
///
/// @param v The cell (borrowed).
/// @param count How many columns it spans.
/// @return `v`, spanning them (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_grid_cell_columns(const View* v, int32_t count) {
    MOD_TRY(v->grid_cell_columns(count))
}
/// Wraps `View::grid_cell_alignment()`: where this view sits in its grid cell, instead of its row's, column's or
/// grid's alignment.
///
/// @param v The cell (borrowed).
/// @param a Where it sits.
/// @return `v` with the alignment (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_grid_cell_alignment(const View* v, ElgAlignment a) {
    MOD_TRY(v->grid_cell_alignment(alignment(a)))
}
/// Wraps `View::grid_column_alignment()`: the horizontal alignment of every cell in this cell's Grid column.
///
/// @param v The cell (borrowed).
/// @param a `HorizontalAlignment`.
/// @return `v` with the alignment (a new view, owned by the caller), or null on failure.
ELG_EXPORT View* elg_mod_grid_column_alignment(const View* v, int32_t a) {
    MOD_TRY(v->grid_column_alignment(static_cast<HorizontalAlignment>(a)))
}

// --- ABI layout ---

/// Reports the size and alignment of each C struct in this file, for elegance-sys's layout test (which
/// compares them with its Rust declarations).
///
/// @param out Called once per struct with a "name size align" line.
/// @param ctx Passed to `out`.
ELG_EXPORT void elg_layouts_views(ElgStrSink out, void* ctx) {
    ELG_TRY
    ELG_LAYOUT(ElgTextFieldOptions);
    ELG_LAYOUT(ElgNumberInputOptions);
    ELG_LAYOUT(ElgOptNumber);
    ELG_LAYOUT(ElgForEachItem);
    ELG_LAYOUT(ElgStackSeparators);
    ELG_LAYOUT(ElgSplitViewOptions);
    ELG_LAYOUT(ElgColumnWidth);
    ELG_LAYOUT(ElgNavigationSplitViewOptions);
    ELG_LAYOUT(ElgGestureOptions);
    ELG_LAYOUT(ElgDragOptions);
    ELG_LAYOUT(ElgLongPressOptions);
    ELG_LAYOUT(ElgFloatingOptions);
    ELG_LAYOUT(ElgTooltipOptions);
    ELG_LAYOUT(ElgItemKey);
    ELG_LAYOUT(ElgItemKeys);
    ELG_LAYOUT(ElgTextRun);
    ELG_LAYOUT(ElgPolyline);
    ELG_LAYOUT(ElgAsyncImagePhase);
    ELG_LAYOUT(ElgIconFont);
    ELG_LAYOUT(ElgIconWeight);
    ELG_LAYOUT(ElgIconVariant);
    ELG_LAYOUT(ElgFrameOptions);
    ELG_LAYOUT(ElgDropFiles);
    ELG_LAYOUT(ElgDropText);
    ELG_LAYOUT(ElgPreviewDesc);
    ELG_LAYOUT(ElgGridItem);
    ELG_LAYOUT(ElgGridTrack);
    ELG_LAYOUT(ElgGridDesc);
    ELG_CATCH_VOID
}
