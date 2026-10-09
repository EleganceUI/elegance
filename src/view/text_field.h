// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_TEXT_FIELD_H
#define ELG_VIEW_TEXT_FIELD_H

// TextField (design.md §11): a TextEditor holds the editing state; a TextEditorElement draws it
// where the style puts the field; the TextFieldElement around the style's body is the focusable
// control that takes the input.

#include "text/text_layout.h"
#include "text/text_system.h"
#include "view/element.h"
#include "view/focus.h"
#include "view/window_services.h"

#include <elg/environment.h>
#include <elg/events.h>
#include <elg/reactive.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace elg::detail {

class TextEditorElement;

// A single line of editable text: the text (synced with the binding), caret and selection (byte
// offsets on cluster boundaries), the IME composition, undo history, and its shaped layout.
class TextEditor {
public:
    TextEditor(TextSystem& text, Binding<std::string> binding, std::string placeholder,
               std::function<void()> on_submit, WindowServices* services, FocusManager* focus,
               EnvironmentValues environment, bool secure = false);

    void set_font(const Font& font);
    // Where the text is shown (fonts and colors come from there).
    void set_environment(EnvironmentValues environment) { environment_ = std::move(environment); }
    // The binding changed from outside (not by editing).
    void set_text(std::string value);

    // Input, from the TextFieldElement.
    EventResult key(const KeyEvent& event);
    EventResult text_input(const TextInputEvent& event);
    EventResult pointer(const PointerEvent& event);
    void focus_changed(bool focused);

    // Layout and drawing, from the TextEditorElement.
    void attach(TextEditorElement* element) noexcept { element_ = element; }
    TextEditorElement* attached() const noexcept { return element_; }
    Size measure(ProposedSize proposal) const;
    void arranged(Rect frame);
    void paint(DisplayList& list, Rect frame) const;

private:
    struct Stop {
        std::size_t byte; // into the displayed text (secure: into the text itself)
        float x;          // points from the start
    };
    struct Snapshot {
        std::string text;
        std::size_t caret;
        std::size_t anchor;
    };

    bool enabled() const;
    bool composing() const noexcept { return !composition_.empty(); }
    std::size_t selection_start() const noexcept { return std::min(caret_, anchor_); }
    std::size_t selection_end() const noexcept { return std::max(caret_, anchor_); }

    // The text as shown: the composition is inserted at the caret.
    std::string displayed() const;
    void reshape();
    float x_at(std::size_t display_byte) const;
    std::size_t byte_at(float x) const; // nearest caret stop to `x` (points from the start)
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
    bool secure_; // shown as bullets; no copying, no IME composition
    Binding<std::string> binding_;
    std::function<void()> on_submit_;
    WindowServices* services_;
    FocusManager* focus_;
    EnvironmentValues environment_;
    TextEditorElement* element_ = nullptr;

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

    ShapedParagraph shaped_;
    std::vector<Stop> stops_;
    float width_ = 0;
    ShapedParagraph placeholder_shaped_;
    float placeholder_width_ = 0;

    Rect frame_;
    float scroll_ = 0; // when the text is wider than the field
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
                     State<bool> hovered, std::function<bool()> enabled, WindowServices* services)
        : editor_(std::move(editor)), content_(std::move(content)), focused_(std::move(focused)),
          hovered_(std::move(hovered)), enabled_(std::move(enabled)), services_(services) {
        adopt(*content_);
    }

    EventHandler* event_handler() noexcept override { return this; }
    HoverDelay effective_hover_delay() const override { return {0, 0}; } // the I-beam shows at once
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override;

private:
    EventResult pointer(const PointerEvent& event, Rect frame) override;
    EventResult hover(bool inside) override;
    EventResult key(const KeyEvent& event) override;
    bool focusable() const override;
    EventResult focus(bool focused) override;
    bool accepts_text_input() const override { return true; }
    EventResult text_input(const TextInputEvent& event) override;

    std::shared_ptr<TextEditor> editor_;
    std::unique_ptr<Element> content_;
    State<bool> focused_;
    State<bool> hovered_;
    std::function<bool()> enabled_;
    WindowServices* services_;
};

} // namespace elg::detail

#endif // ELG_VIEW_TEXT_FIELD_H
