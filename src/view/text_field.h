// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_TEXT_FIELD_H
#define ELG_VIEW_TEXT_FIELD_H

// TextField and TextArea (design.md §11): a TextEditor holds the editing state; a
// TextEditorElement draws it where the style puts the field; the TextFieldElement around the
// style's body is the focusable control that takes the input.

#include "text/styled_layout.h"
#include "text/text_layout.h"
#include "text/text_system.h"
#include "view/element.h"
#include "view/focus.h"
#include "view/window_services.h"

#include <elg/environment.h>
#include <elg/events.h>
#include <elg/reactive.h>
#include <elg/text.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace elg::detail {

class TextEditorElement;

// The editable text: the text (synced with the binding), caret and selection (byte offsets on
// cluster boundaries), the IME composition, undo history, and its styled, wrapped layout.
//
// What's shown can differ from the text: spans with a label (TextFieldOptions::styler) show the
// label and are edited as one unit, and secure fields show a bullet per character. Segments map
// between the two; the caret and selection live in the text, drawing works in what's shown.
class TextEditor {
public:
    // `area`: a TextArea (Enter starts a new line, and it takes the height it's offered).
    TextEditor(TextSystem& text, Binding<std::string> binding, std::function<void()> on_submit,
               WindowServices* services, FocusManager* focus, EnvironmentValues environment, bool area = false);

    // TextFieldOptions (and TextAreaOptions) as they change.
    void set_placeholder(std::string placeholder);
    void set_secure(bool secure);
    void set_line_limit(int lines);
    void set_styler(std::function<std::vector<TextSpan>(std::string_view)> styler);
    void set_font(const Font& font);
    // Where the text is shown (fonts and colors come from there).
    void set_environment(EnvironmentValues environment) { environment_ = std::move(environment); }
    // The binding changed from outside (not by editing).
    void set_text(std::string value);
    // Assistive technology: the text (empty for secure fields), the placeholder, and replacing it.
    std::string accessible_text() const { return secure_ ? std::string() : text_; }
    const std::string& placeholder() const noexcept { return placeholder_; }
    bool secure() const noexcept { return secure_; }
    bool area() const noexcept { return area_; }
    void replace_all(std::string value);

    // Input, from the TextFieldElement.
    EventResult key(const KeyEvent& event);
    EventResult text_input(const TextInputEvent& event);
    EventResult pointer(const PointerEvent& event);
    void focus_changed(bool focused);
    // Wheel and trackpad scrolling through more lines than show; unconsumed at the ends.
    EventResult scroll(const ScrollEvent& event);

    // Layout and drawing, from the TextEditorElement.
    void attach(TextEditorElement* element) noexcept { element_ = element; }
    TextEditorElement* attached() const noexcept { return element_; }
    Size measure(ProposedSize proposal) const;
    void arranged(Rect frame);
    void paint(DisplayList& list, Rect frame) const;

private:
    struct Snapshot {
        std::string text;
        std::size_t caret;
        std::size_t anchor;
    };
    // A stretch of the text and how it's shown; atomic ones (labels, bullets) can't be entered.
    struct Segment {
        std::size_t text_begin;
        std::size_t text_end;
        std::size_t shown_begin;
        std::size_t shown_end;
        bool atomic;
    };

    bool enabled() const;
    // Secure fields are one line.
    bool multiline() const noexcept { return !secure_ && (area_ || max_lines_ > 1); }
    bool composing() const noexcept { return !composition_.empty(); }
    std::size_t selection_start() const noexcept { return std::min(caret_, anchor_); }
    std::size_t selection_end() const noexcept { return std::max(caret_, anchor_); }
    std::string clean(std::string text) const; // what the field takes: one line, unless multiline
    void lines_changed(); // secure or the line limit changed: the text may have to become one line
    void shape_placeholder();

    // Text <-> shown bytes. Bytes inside an atomic segment go to its nearer end (or `toward_end`).
    std::size_t shown_without_composition(std::size_t text_byte) const;
    std::size_t to_shown(std::size_t text_byte) const;
    std::size_t to_text(std::size_t shown_byte, std::optional<bool> toward_end = std::nullopt) const;
    // Inside an atomic segment: out of it, toward its end or start.
    std::size_t out_of_atomic(std::size_t text_byte, bool toward_end) const;

    void reshape();                 // the text or style changed: segments, shown text, shaping
    void relayout(float width);     // line breaking for this width, caret stops
    TextLayoutResult layout_for(std::optional<float> width) const;
    const CaretStop& stop_at(std::size_t shown_byte) const; // where a shown byte's caret goes
    std::size_t line_of(std::size_t text_byte) const;
    std::size_t byte_at(Point local) const; // nearest caret stop to a point (in the text's coordinates)
    std::size_t byte_on_line(std::size_t line, float x) const;
    std::size_t previous_stop(std::size_t byte) const;
    std::size_t next_stop(std::size_t byte) const;
    std::size_t previous_word(std::size_t byte) const;
    std::size_t next_word(std::size_t byte) const;
    void select_word(std::size_t byte);

    void move_caret(std::size_t byte, bool extend);
    void replace_selection(std::string text, bool typing);
    void erase(std::size_t begin, std::size_t end);
    void remember(bool typing);
    void restore(std::vector<Snapshot>& from, std::vector<Snapshot>& to);
    void changed(); // the text changed: write the binding, reshape, show the caret
    void restart_blink();
    void layout_changed();

    TextSystem* text_system_;
    bool area_;
    bool secure_ = false; // shown as bullets; no copying, no IME composition
    Binding<std::string> binding_;
    std::function<void()> on_submit_;
    WindowServices* services_;
    FocusManager* focus_;
    EnvironmentValues environment_;
    TextEditorElement* element_ = nullptr;
    int max_lines_ = 1;
    std::function<std::vector<TextSpan>(std::string_view)> styler_;

    Font font_ = Font::system(15);
    const FontFace* face_ = nullptr;
    float size_ = 15;
    float ascent_ = 12;
    float line_height_ = 20;

    std::string text_;
    std::string placeholder_;
    std::size_t caret_ = 0;
    std::size_t anchor_ = 0;
    std::string composition_;
    std::size_t composition_cursor_ = 0;

    // What's shown, shaped (width-independent) and laid out (for layout_width_).
    std::vector<Segment> segments_;
    std::string shown_;
    std::vector<TextSpan> spans_;  // the styler's, with shown byte ranges
    StyledShape shape_;
    std::vector<DrawStyle> styles_template_; // colors filled in when drawn
    std::vector<std::optional<Color>> style_colors_; // each style's own color; none: the foreground
    TextLayoutResult layout_;
    std::optional<float> layout_width_;
    std::vector<CaretStop> stops_;  // shown bytes
    std::vector<std::size_t> text_stops_; // the caret stops in the text, sorted
    float natural_width_ = 0;
    ShapedParagraph placeholder_shaped_;
    float placeholder_width_ = 0;

    Rect frame_;
    float scroll_x_ = 0; // single line: when the text is wider than the field
    float scroll_y_ = 0; // multiline: when it has more lines than shown
    // Where the caret was and how tall the field was when the caret was last scrolled into sight:
    // multiline fields follow the caret when either changes, and the wheel otherwise.
    std::size_t revealed_caret_ = std::string::npos;
    float revealed_height_ = -1;
    bool focused_ = false;
    bool dragging_ = false;
    double blink_start_ = 0;
    std::optional<Rect> input_area_;
    float input_cursor_ = 0;

    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    bool typing_ = false; // the last change was typing (consecutive typing undoes together)
};

// Where the text goes inside the style's body.
class TextEditorElement final : public Element {
public:
    explicit TextEditorElement(std::shared_ptr<TextEditor> editor) : editor_(std::move(editor)) {
        editor_->attach(this);
    }
    bool always_arranges() const override { return true; } // keeps the caret in sight
    ~TextEditorElement() override {
        if (editor_->attached() == this) {
            editor_->attach(nullptr);
        }
    }

    void text_changed() { invalidate_layout(); }

protected:
    Size measure(ProposedSize proposal) override { return editor_->measure(proposal); }
    void arrange(Rect bounds, ProposedSize) override { editor_->arranged(bounds); }
    void paint_impl(DisplayList& list) const override { editor_->paint(list, frame()); }
    bool hit_impl(Point point, std::vector<Element*>&) override;

private:
    std::shared_ptr<TextEditor> editor_;
};

// The control: focusable, takes keys, text, and the pointer (anywhere in the style's body).
class TextFieldElement final : public Element, private EventHandler {
public:
    TextFieldElement(std::shared_ptr<TextEditor> editor, std::unique_ptr<Element> content, State<bool> focused,
                     State<bool> hovered, std::function<bool()> enabled)
        : editor_(std::move(editor)), content_(std::move(content)), focused_(std::move(focused)),
          hovered_(std::move(hovered)), enabled_(std::move(enabled)) {
        adopt(*content_);
    }

    EventHandler* event_handler() noexcept override { return this; }
    HoverDelay effective_hover_delay() const override { return {0, 0}; } // the I-beam shows at once
    // TextFieldOptions::on_key: offered each key before the editor.
    void set_key_filter(std::function<bool(const KeyEvent&)> filter) { key_filter_ = std::move(filter); }
    void accessibility(AccessibilityInfo& out) const override;
    bool accessibility_action(const AccessibilityAction& action) override;
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override;

private:
    EventResult pointer(const PointerEvent& event, Rect frame) override;
    EventResult hover(bool inside) override;
    PointerStyle pointer_style(Point) const override;
    EventResult key(const KeyEvent& event) override;
    EventResult scroll(const ScrollEvent& event, Rect frame) override;
    bool focusable() const override;
    EventResult focus(bool focused) override;
    bool accepts_text_input() const override { return true; }
    EventResult text_input(const TextInputEvent& event) override;

    std::shared_ptr<TextEditor> editor_;
    std::unique_ptr<Element> content_;
    std::function<bool(const KeyEvent&)> key_filter_;
    State<bool> focused_;
    State<bool> hovered_;
    std::function<bool()> enabled_;
};

} // namespace elg::detail

#endif // ELG_VIEW_TEXT_FIELD_H
