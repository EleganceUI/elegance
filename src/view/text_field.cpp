// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "view/text_field.h"

#include "text/glyph_draw.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <string_view>

namespace elg::detail {

namespace {

constexpr double kBlinkHalfPeriod = 0.53; // seconds the caret is shown, then hidden
constexpr std::size_t kUndoLimit = 200;
constexpr float kCaretWidth = 1.5f;
constexpr float kMinWidth = 40;

bool contains(const Rect& rect, Point point) {
    return point.x >= rect.x && point.y >= rect.y && point.x < rect.max_x() && point.y < rect.max_y();
}

// Word characters for word movement and double-click: letters, digits, '_', and anything
// non-ASCII (so words in other scripts hold together).
bool is_word_byte(char c) {
    const auto u = static_cast<unsigned char>(c);
    return u >= 0x80 || std::isalnum(u) || c == '_';
}

// A single line: line breaks and tabs become spaces.
std::string single_line(std::string text) {
    std::erase(text, '\r');
    std::ranges::replace(text, '\n', ' ');
    std::ranges::replace(text, '\t', ' ');
    return text;
}

bool is_continuation(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

constexpr std::string_view kBullet = "\xE2\x80\xA2"; // U+2022, one per character in secure fields

// Moves `byte` back onto a UTF-8 character boundary.
std::size_t to_boundary(const std::string& text, std::size_t byte) {
    byte = std::min(byte, text.size());
    while (byte > 0 && byte < text.size() && is_continuation(text[byte])) {
        --byte;
    }
    return byte;
}

} // namespace

TextEditor::TextEditor(TextSystem& text, Binding<std::string> binding, std::string placeholder,
                       std::function<void()> on_submit, WindowServices* services, FocusManager* focus,
                       EnvironmentValues environment, bool secure)
    : text_system_(&text), secure_(secure), binding_(std::move(binding)), on_submit_(std::move(on_submit)), services_(services),
      focus_(focus), environment_(std::move(environment)), placeholder_(single_line(std::move(placeholder))) {}

bool TextEditor::enabled() const {
    return !untrack([&] { return environment_.get<DisabledKey>(); });
}

void TextEditor::set_font(const Font& font) {
    face_ = &text_system_->fonts.resolve(font);
    size_ = font.size();
    ascent_ = face_->ascender() * size_;
    line_height_ = (face_->ascender() - face_->descender() + face_->line_gap()) * size_;
    const std::vector<ShapedParagraph> placeholder = shape_text(*face_, placeholder_, &text_system_->fonts);
    placeholder_shaped_ = placeholder.empty() ? ShapedParagraph{} : placeholder.front();
    placeholder_width_ = 0;
    for (const ShapedGlyph& glyph : placeholder_shaped_.glyphs) {
        placeholder_width_ += glyph.advance * size_;
    }
    reshape();
}

void TextEditor::set_text(std::string value) {
    value = single_line(std::move(value));
    if (value == text_) {
        return; // our own write coming back
    }
    text_ = std::move(value);
    caret_ = to_boundary(text_, caret_);
    anchor_ = to_boundary(text_, anchor_);
    composition_.clear();
    typing_ = false;
    reshape();
}

// --- Layout ---

std::string TextEditor::displayed() const {
    if (secure_) {
        std::string bullets;
        for (const char c : text_) {
            if (!is_continuation(c)) {
                bullets += kBullet;
            }
        }
        return bullets;
    }
    if (!composing()) {
        return text_;
    }
    std::string shown = text_;
    shown.insert(caret_, composition_);
    return shown;
}

void TextEditor::reshape() {
    stops_.clear();
    width_ = 0;
    shaped_ = {};
    const std::string shown = displayed();
    if (face_ && !shown.empty()) {
        std::vector<ShapedParagraph> shaped = shape_text(*face_, shown, &text_system_->fonts);
        if (!shaped.empty()) {
            shaped_ = std::move(shaped.front());
        }
    }
    // A caret stop at the start of each cluster (a character with its combining marks, or a
    // ligature), and at the end.
    float pen = 0;
    for (std::size_t i = 0; i < shaped_.glyphs.size(); ++i) {
        const ShapedGlyph& glyph = shaped_.glyphs[i];
        if (i == 0 || glyph.cluster != shaped_.glyphs[i - 1].cluster) {
            stops_.push_back({glyph.cluster, pen});
        }
        pen += glyph.advance * size_;
    }
    if (stops_.empty() || stops_.front().byte != 0) {
        stops_.insert(stops_.begin(), {0, 0});
    }
    stops_.push_back({shown.size(), pen});
    if (secure_) {
        // Bullet n stands for character n: keep the stops in the text's bytes.
        std::vector<std::size_t> starts;
        for (std::size_t i = 0; i < text_.size(); ++i) {
            if (!is_continuation(text_[i])) {
                starts.push_back(i);
            }
        }
        starts.push_back(text_.size());
        for (Stop& stop : stops_) {
            stop.byte = starts[std::min(stop.byte / kBullet.size(), starts.size() - 1)];
        }
    }
    width_ = pen;
    layout_changed();
}

void TextEditor::layout_changed() {
    if (element_) {
        element_->text_changed();
    }
}

float TextEditor::x_at(std::size_t byte) const {
    float x = 0;
    for (const Stop& stop : stops_) {
        if (stop.byte > byte) {
            break;
        }
        x = stop.x;
    }
    return x;
}

std::size_t TextEditor::byte_at(float x) const {
    std::size_t best = 0;
    float best_distance = std::numeric_limits<float>::max();
    for (const Stop& stop : stops_) {
        if (const float distance = std::abs(stop.x - x); distance < best_distance) {
            best_distance = distance;
            best = stop.byte;
        }
    }
    return best;
}

std::size_t TextEditor::previous_stop(std::size_t byte) const {
    std::size_t result = 0;
    for (const Stop& stop : stops_) {
        if (stop.byte >= byte) {
            break;
        }
        result = stop.byte;
    }
    return result;
}

std::size_t TextEditor::next_stop(std::size_t byte) const {
    for (const Stop& stop : stops_) {
        if (stop.byte > byte) {
            return stop.byte;
        }
    }
    return text_.size();
}

std::size_t TextEditor::previous_word(std::size_t byte) const {
    if (secure_) {
        return 0; // words would reveal the text's shape
    }
    while (byte > 0 && !is_word_byte(text_[byte - 1])) {
        --byte;
    }
    while (byte > 0 && is_word_byte(text_[byte - 1])) {
        --byte;
    }
    return to_boundary(text_, byte);
}

std::size_t TextEditor::next_word(std::size_t byte) const {
    if (secure_) {
        return text_.size();
    }
    while (byte < text_.size() && is_word_byte(text_[byte])) {
        ++byte;
    }
    while (byte < text_.size() && !is_word_byte(text_[byte])) {
        ++byte;
    }
    return to_boundary(text_, byte);
}

void TextEditor::select_word(std::size_t byte) {
    if (secure_) {
        anchor_ = 0;
        caret_ = text_.size();
        return;
    }
    // The run of word (or of non-word) characters around `byte`.
    const bool word = byte < text_.size() ? is_word_byte(text_[byte]) : byte > 0 && is_word_byte(text_[byte - 1]);
    std::size_t begin = byte;
    std::size_t end = byte;
    while (begin > 0 && is_word_byte(text_[begin - 1]) == word) {
        --begin;
    }
    while (end < text_.size() && is_word_byte(text_[end]) == word) {
        ++end;
    }
    anchor_ = to_boundary(text_, begin);
    caret_ = to_boundary(text_, end);
}

Size TextEditor::measure(ProposedSize proposal) const {
    // Takes the width it's offered (like SwiftUI); ideally, its text's.
    const float natural = std::max({width_, placeholder_width_, kMinWidth}) + kCaretWidth;
    return {proposal.width.value_or(natural), line_height_};
}

void TextEditor::arranged(Rect frame) {
    frame_ = frame;
    // Keep the caret in sight when the text is wider than the field.
    const std::size_t caret = caret_ + (composing() ? composition_cursor_ : 0);
    const float caret_x = x_at(caret);
    const float room = std::max(frame.width - kCaretWidth, 0.f);
    if (width_ <= room) {
        scroll_ = 0;
    } else {
        if (caret_x - scroll_ < 0) {
            scroll_ = caret_x;
        } else if (caret_x - scroll_ > room) {
            scroll_ = caret_x - room;
        }
        scroll_ = std::clamp(scroll_, 0.f, width_ - room);
    }
    // Tell the IME where we are (for its candidate list), when that changed.
    if (focused_ && services_) {
        const float cursor = caret_x - scroll_;
        if (!input_area_ || *input_area_ != frame || input_cursor_ != cursor) {
            input_area_ = frame;
            input_cursor_ = cursor;
            services_->set_text_input_area(frame, cursor);
        }
    }
}

void TextEditor::paint(DisplayList& list, Rect frame) const {
    const float scale = list.scale > 0 ? list.scale : 1.f;
    const auto snap = [&](float value) { return std::round(value * scale) / scale; };
    const bool active = enabled();
    const Color tint = environment_.get<TintKey>();
    const Color foreground = active ? environment_.get<ForegroundKey>() : environment_.color(ThemeColor::tertiary);
    const float origin = frame.x - scroll_;
    const float baseline = snap(frame.y + ascent_);

    list.commands.push_back(PushClipCommand{Rect{frame.x - kCaretWidth, frame.y, frame.width + 2 * kCaretWidth, frame.height}, {}});

    if (focused_ && !composing() && caret_ != anchor_) {
        const float x0 = x_at(selection_start());
        const float x1 = x_at(selection_end());
        Color selection = tint;
        selection.a *= 0.35f;
        list.commands.push_back(RectCommand{{origin + x0, frame.y, x1 - x0, frame.height}, {}, ShapeStyle(selection),
                                            Color::clear, 0});
    }

    const auto draw = [&](const ShapedParagraph& paragraph, Color color) {
        float pen = origin;
        for (const ShapedGlyph& glyph : paragraph.glyphs) {
            draw_glyph(list, text_system_->atlas, glyph.face ? *glyph.face : *face_, glyph, pen, baseline, size_, color);
            pen += glyph.advance * size_;
        }
    };
    if (face_) {
        if (shaped_.glyphs.empty()) {
            draw(placeholder_shaped_, environment_.color(ThemeColor::tertiary));
        } else {
            draw(shaped_, foreground);
        }
    }

    if (composing()) {
        // The IME's text is underlined until it's committed.
        const float x0 = x_at(caret_);
        const float x1 = x_at(caret_ + composition_.size());
        list.commands.push_back(RectCommand{{origin + x0, snap(baseline + 2), x1 - x0, 1}, {}, ShapeStyle(foreground),
                                            Color::clear, 0});
    }

    if (focused_ && active && (caret_ == anchor_ || composing())) {
        const double elapsed = std::max(frame_time() - blink_start_, 0.0);
        const auto phase = static_cast<long long>(elapsed / kBlinkHalfPeriod);
        if (phase % 2 == 0) {
            const float x = snap(origin + x_at(caret_ + (composing() ? composition_cursor_ : 0)));
            list.commands.push_back(
                RectCommand{{x, frame.y, kCaretWidth, frame.height}, {}, ShapeStyle(tint), Color::clear, 0});
        }
        if (services_) {
            services_->wake_at(blink_start_ + static_cast<double>(phase + 1) * kBlinkHalfPeriod);
        }
    }

    list.commands.push_back(PopClipCommand{});
}

// --- Editing ---

void TextEditor::restart_blink() {
    blink_start_ = frame_time(); // the caret shows right after any change
}

void TextEditor::move_caret(std::size_t byte, bool extend) {
    caret_ = byte;
    if (!extend) {
        anchor_ = caret_;
    }
    typing_ = false;
    restart_blink();
}

void TextEditor::remember(bool typing) {
    if (!(typing && typing_)) {
        undo_.push_back({text_, caret_, anchor_});
        if (undo_.size() > kUndoLimit) {
            undo_.erase(undo_.begin());
        }
    }
    redo_.clear();
    typing_ = typing;
}

void TextEditor::restore(std::vector<Snapshot>& from, std::vector<Snapshot>& to) {
    if (from.empty()) {
        return;
    }
    to.push_back({text_, caret_, anchor_});
    Snapshot snapshot = std::move(from.back());
    from.pop_back();
    text_ = std::move(snapshot.text);
    caret_ = snapshot.caret;
    anchor_ = snapshot.anchor;
    typing_ = false;
    changed();
}

void TextEditor::replace_all(std::string value) {
    text_ = std::move(value);
    caret_ = anchor_ = text_.size();
    changed();
}

void TextFieldElement::accessibility(AccessibilityInfo& out) const {
    out.role = editor_->secure() ? AccessRole::password_field : AccessRole::text_field;
    out.value = editor_->accessible_text();
    out.placeholder = editor_->placeholder();
    out.label = editor_->placeholder();
    out.disabled = enabled_ && !untrack(enabled_);
    out.can_focus = out.can_set_value = !out.disabled;
}

bool TextFieldElement::accessibility_action(const AccessibilityAction& action) {
    if (action.kind != AccessibilityAction::Kind::set_value || (enabled_ && !untrack(enabled_))) {
        return false;
    }
    editor_->replace_all(action.text);
    return true;
}

void TextEditor::changed() {
    binding_.set(text_);
    reshape();
    restart_blink();
}

void TextEditor::replace_selection(std::string text, bool typing) {
    text = single_line(std::move(text));
    const std::size_t begin = selection_start();
    const std::size_t end = selection_end();
    if (text.empty() && begin == end) {
        return;
    }
    remember(typing && begin == end);
    text_.replace(begin, end - begin, text);
    caret_ = anchor_ = begin + text.size();
    changed();
}

void TextEditor::erase(std::size_t begin, std::size_t end) {
    if (begin >= end) {
        return;
    }
    remember(false);
    text_.erase(begin, end - begin);
    caret_ = anchor_ = begin;
    changed();
}

EventResult TextEditor::key(const KeyEvent& event) {
    if (!enabled()) {
        return {};
    }
    const bool shift = has(event.modifiers, KeyModifiers::shift);
    const bool primary = has(event.modifiers, primary_modifier());
#if defined(__APPLE__)
    const bool word = has(event.modifiers, KeyModifiers::alt);
#else
    const bool word = has(event.modifiers, KeyModifiers::control);
#endif
    // Ctrl+Alt is AltGr on Windows: it types characters.
    const bool shortcut = (has(event.modifiers, KeyModifiers::control) && !has(event.modifiers, KeyModifiers::alt)) ||
                          has(event.modifiers, KeyModifiers::super);
    const bool printable = (event.key >= Key::a && event.key <= Key::digit9) || event.key == Key::space ||
                           event.key == Key::unknown;
    const EventResult handled{.consumed = true, .redraw = true};

    if (composing()) {
        return {.consumed = true}; // the input method has the keys
    }
    if (event.kind == KeyEvent::Kind::down) {
        const std::size_t begin = selection_start();
        const std::size_t end = selection_end();
        const bool selection = begin != end;
        switch (event.key) {
        case Key::left:
            if (selection && !shift) {
                move_caret(begin, false);
            } else {
                move_caret(word ? previous_word(caret_) : previous_stop(caret_), shift);
            }
            return handled;
        case Key::right:
            if (selection && !shift) {
                move_caret(end, false);
            } else {
                move_caret(word ? next_word(caret_) : next_stop(caret_), shift);
            }
            return handled;
        case Key::home:
        case Key::up: move_caret(0, shift); return handled;
        case Key::end:
        case Key::down: move_caret(text_.size(), shift); return handled;
        case Key::backspace:
            if (selection) {
                erase(begin, end);
            } else {
                erase(word ? previous_word(caret_) : previous_stop(caret_), caret_);
            }
            return handled;
        case Key::forward_delete:
            if (selection) {
                erase(begin, end);
            } else {
                erase(caret_, word ? next_word(caret_) : next_stop(caret_));
            }
            return handled;
        case Key::enter:
            if (on_submit_) {
                on_submit_();
            }
            return handled;
        case Key::a:
            if (primary) {
                anchor_ = 0;
                caret_ = text_.size();
                return handled;
            }
            break;
        case Key::c:
        case Key::x:
            if (primary) {
                if (selection && services_ && !secure_) { // passwords don't leave the field
                    services_->set_clipboard_text(text_.substr(begin, end - begin));
                    if (event.key == Key::x) {
                        erase(begin, end);
                    }
                }
                return handled;
            }
            break;
        case Key::v:
            if (primary) {
                if (services_) {
                    replace_selection(services_->clipboard_text(), false);
                }
                return handled;
            }
            break;
        case Key::z:
            if (primary) {
                shift ? restore(redo_, undo_) : restore(undo_, redo_);
                return handled;
            }
            break;
        case Key::y:
            if (primary) {
                restore(redo_, undo_);
                return handled;
            }
            break;
        default: break;
        }
    }
    // Typing arrives as text input; keep its keys from reaching key handlers around the field.
    return {.consumed = printable && !shortcut};
}

EventResult TextEditor::text_input(const TextInputEvent& event) {
    if (!enabled()) {
        return {};
    }
    if (event.kind == TextInputEvent::Kind::commit) {
        composition_.clear();
        replace_selection(event.text, true);
        return {.consumed = true, .redraw = true};
    }
    if (secure_) {
        return {.consumed = true}; // no IME in password fields; committed text still arrives
    }
    // Composition: starting one replaces the selection, like typing would.
    if (!composing() && !event.text.empty() && caret_ != anchor_) {
        erase(selection_start(), selection_end());
    }
    composition_ = single_line(event.text);
    composition_cursor_ = std::min(event.cursor, composition_.size());
    reshape();
    restart_blink();
    return {.consumed = true, .redraw = true};
}

EventResult TextEditor::pointer(const PointerEvent& event) {
    if (!enabled() || event.button != MouseButton::left) {
        return {};
    }
    const auto byte_under = [&] { return byte_at(event.location.x - frame_.x + scroll_); };
    switch (event.kind) {
    case PointerEvent::Kind::down:
        if (composing()) {
            return {.consumed = true};
        }
        if (event.click_count >= 3) {
            anchor_ = 0;
            caret_ = text_.size();
        } else if (event.click_count == 2) {
            select_word(byte_under());
        } else {
            move_caret(byte_under(), has(event.modifiers, KeyModifiers::shift));
            dragging_ = true;
        }
        typing_ = false;
        restart_blink();
        return {.consumed = true, .redraw = true};
    case PointerEvent::Kind::move:
        if (!dragging_) {
            return {};
        }
        caret_ = byte_under(); // dragging past the ends scrolls, since layout follows the caret
        restart_blink();
        return {.consumed = true, .redraw = true};
    case PointerEvent::Kind::up:
    case PointerEvent::Kind::cancel: dragging_ = false; return {.consumed = true};
    }
    return {};
}

void TextEditor::focus_changed(bool focused) {
    focused_ = focused;
    dragging_ = false;
    input_area_.reset();
    if (focused) {
        // Tabbing in selects everything, like the web and native fields; clicking places the
        // caret (the pointer handler does that next).
        if (focus_ && focus_->ring_visible()) {
            anchor_ = 0;
            caret_ = text_.size();
        }
        restart_blink();
    } else if (composing()) {
        composition_.clear(); // the IME cancels it when text input stops
        reshape();
    }
    layout_changed();
}

// --- Elements ---

bool TextEditorElement::hit_impl(Point point, std::vector<Element*>&) {
    return contains(frame(), point);
}

bool TextFieldElement::hit_impl(Point point, std::vector<Element*>& path) {
    if (!contains(frame(), point)) {
        return false;
    }
    content_->hit_test(point, path);
    return true; // padding inside the style's box is part of the field
}

bool TextFieldElement::focusable() const {
    return untrack(enabled_);
}

EventResult TextFieldElement::pointer(const PointerEvent& event, Rect) {
    return editor_->pointer(event);
}

EventResult TextFieldElement::hover(bool inside) {
    hovered_ = inside;
    if (services_) {
        services_->set_cursor(inside ? CursorShape::text : CursorShape::arrow);
    }
    return {};
}

EventResult TextFieldElement::key(const KeyEvent& event) {
    return editor_->key(event);
}

EventResult TextFieldElement::focus(bool focused) {
    focused_ = focused;
    editor_->focus_changed(focused);
    return {.redraw = true};
}

EventResult TextFieldElement::text_input(const TextInputEvent& event) {
    return editor_->text_input(event);
}

} // namespace elg::detail
