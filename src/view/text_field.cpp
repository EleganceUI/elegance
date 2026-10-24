// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "view/text_field.h"

#include "text/glyph_draw.h"
#include "text/styled_layout.h"

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

void TextEditor::configure(int max_lines, std::function<std::vector<TextSpan>(std::string_view)> styler) {
    max_lines_ = std::max(1, max_lines);
    styler_ = secure_ ? nullptr : std::move(styler); // a password's shape stays hidden
    text_ = clean(std::move(text_));
    reshape();
}

std::string TextEditor::clean(std::string text) const {
    if (!multiline()) {
        return single_line(std::move(text));
    }
    std::erase(text, '\r');
    std::ranges::replace(text, '\t', ' ');
    return text;
}

void TextEditor::set_font(const Font& font) {
    font_ = font;
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
    value = clean(std::move(value));
    if (value == text_) {
        return; // our own write coming back
    }
    text_ = std::move(value);
    // Replaced from outside (a completion, a reset, text to edit): the caret goes to the end,
    // where typing carries on.
    caret_ = anchor_ = text_.size();
    composition_.clear();
    typing_ = false;
    reshape();
}

// --- What's shown ---

std::size_t TextEditor::shown_without_composition(std::size_t text_byte) const {
    for (const Segment& segment : segments_) {
        if (text_byte <= segment.text_end) {
            if (segment.atomic) {
                return text_byte >= segment.text_end ? segment.shown_end : segment.shown_begin;
            }
            return segment.shown_begin + (text_byte - segment.text_begin);
        }
    }
    return segments_.empty() ? 0 : segments_.back().shown_end;
}

std::size_t TextEditor::to_shown(std::size_t text_byte) const {
    std::size_t shown = shown_without_composition(text_byte);
    if (composing() && text_byte > caret_) {
        shown += composition_.size();
    }
    return shown;
}

std::size_t TextEditor::to_text(std::size_t shown_byte, std::optional<bool> toward_end) const {
    if (composing()) {
        const std::size_t at = shown_without_composition(caret_);
        if (shown_byte >= at && shown_byte <= at + composition_.size()) {
            return caret_;
        }
        if (shown_byte > at + composition_.size()) {
            shown_byte -= composition_.size();
        }
    }
    for (const Segment& segment : segments_) {
        if (shown_byte <= segment.shown_end) {
            if (!segment.atomic) {
                return segment.text_begin + (shown_byte - segment.shown_begin);
            }
            if (shown_byte <= segment.shown_begin) {
                return segment.text_begin;
            }
            if (shown_byte >= segment.shown_end) {
                return segment.text_end;
            }
            const bool end = toward_end ? *toward_end
                                        : (shown_byte - segment.shown_begin) * 2 >= segment.shown_end - segment.shown_begin;
            return end ? segment.text_end : segment.text_begin;
        }
    }
    return text_.size();
}

std::size_t TextEditor::out_of_atomic(std::size_t text_byte, bool toward_end) const {
    for (const Segment& segment : segments_) {
        if (segment.atomic && segment.text_begin < text_byte && text_byte < segment.text_end) {
            return toward_end ? segment.text_end : segment.text_begin;
        }
    }
    return text_byte;
}

void TextEditor::reshape() {
    segments_.clear();
    spans_.clear();
    shown_.clear();
    const auto plain = [&](std::size_t begin, std::size_t end) {
        segments_.push_back({begin, end, shown_.size(), shown_.size() + (end - begin), false});
        shown_.append(text_, begin, end - begin);
    };
    if (secure_) {
        // Bullet n stands for character n.
        for (std::size_t i = 0; i < text_.size();) {
            std::size_t next = i + 1;
            while (next < text_.size() && is_continuation(text_[next])) {
                ++next;
            }
            segments_.push_back({i, next, shown_.size(), shown_.size() + kBullet.size(), true});
            shown_ += kBullet;
            i = next;
        }
    } else {
        std::vector<TextSpan> spans = styler_ ? styler_(text_) : std::vector<TextSpan>{};
        std::size_t at = 0;
        for (TextSpan& span : spans) {
            span.begin = to_boundary(text_, std::min(span.begin, text_.size()));
            span.end = to_boundary(text_, std::min(span.end, text_.size()));
            if (span.begin < at || span.begin >= span.end) {
                continue; // overlapping or empty: the styler's mistake; skip it
            }
            if (span.begin > at) {
                plain(at, span.begin);
            }
            const std::size_t shown_begin = shown_.size();
            if (span.label) {
                segments_.push_back({span.begin, span.end, shown_begin, shown_begin + span.label->size(), true});
                shown_ += *span.label;
            } else {
                plain(span.begin, span.end);
            }
            TextSpan shown_span = span;
            shown_span.begin = shown_begin;
            shown_span.end = shown_.size();
            spans_.push_back(std::move(shown_span));
            at = span.end;
        }
        if (at < text_.size()) {
            plain(at, text_.size());
        }
    }
    // The IME's text goes in at the caret, pushing what's after it along.
    if (composing()) {
        const std::size_t at = shown_without_composition(caret_);
        shown_.insert(at, composition_);
        for (TextSpan& span : spans_) {
            if (span.begin >= at) {
                span.begin += composition_.size();
            }
            if (span.end > at) {
                span.end += composition_.size();
            }
        }
    }

    if (face_) {
        std::vector<StyledRunSpec> runs;
        styles_template_ = {DrawStyle{}};
        style_colors_ = {std::nullopt};
        for (std::size_t i = 0; i < spans_.size(); ++i) {
            const TextSpan& span = spans_[i];
            runs.push_back({.begin = span.begin,
                            .end = span.end,
                            .face = &text_system_->fonts.resolve(run_font(font_, span.style)),
                            .style = static_cast<std::uint32_t>(i + 1),
                            .keep_together = span.style.keep_together || span.style.background.has_value() ||
                                             span.label.has_value()});
            styles_template_.push_back({.background = span.style.background,
                                        .underline = span.style.underline,
                                        .strikethrough = span.style.strikethrough});
            style_colors_.push_back(span.style.color);
        }
        shape_ = shape_styled(shown_, runs, *face_, size_, text_system_->fonts);
        natural_width_ = layout_for(std::nullopt).width;
        relayout(layout_width_.value_or(std::numeric_limits<float>::infinity()));
    }
    layout_changed();
}

TextLayoutResult TextEditor::layout_for(std::optional<float> width) const {
    if (!face_) {
        return {};
    }
    return layout_text(shape_.paragraphs, {.font_size = size_,
                                           .line_height = shape_.line_height,
                                           .max_width = multiline() ? width : std::nullopt});
}

void TextEditor::relayout(float width) {
    layout_ = layout_for(width);
    layout_width_ = width;
    stops_ = caret_stops(shape_, layout_, size_, shown_.size());
    text_stops_.clear();
    for (const CaretStop& stop : stops_) {
        text_stops_.push_back(to_text(stop.byte));
    }
    text_stops_.push_back(0);
    text_stops_.push_back(text_.size());
    std::ranges::sort(text_stops_);
    text_stops_.erase(std::unique(text_stops_.begin(), text_stops_.end()), text_stops_.end());
}

void TextEditor::layout_changed() {
    if (element_) {
        element_->text_changed();
    }
}

const CaretStop& TextEditor::stop_at(std::size_t shown_byte) const {
    // The last exact match: at a wrap, the caret goes to the start of the next line.
    const CaretStop* exact = nullptr;
    const CaretStop* before = &stops_.front();
    for (const CaretStop& stop : stops_) {
        if (stop.byte == shown_byte) {
            exact = &stop;
        } else if (stop.byte < shown_byte) {
            before = &stop;
        }
    }
    return exact ? *exact : *before;
}

std::size_t TextEditor::line_of(std::size_t text_byte) const {
    return stop_at(to_shown(text_byte)).line;
}

std::size_t TextEditor::byte_on_line(std::size_t line, float x) const {
    const CaretStop* best = nullptr;
    for (const CaretStop& stop : stops_) {
        if (stop.line == line && (!best || std::abs(stop.x - x) < std::abs(best->x - x))) {
            best = &stop;
        }
    }
    return best ? to_text(best->byte) : 0;
}

std::size_t TextEditor::byte_at(Point local) const {
    const float line_height = shape_.line_height > 0 ? shape_.line_height : line_height_;
    const std::size_t lines = std::max<std::size_t>(layout_.lines.size(), 1);
    const auto line = static_cast<std::size_t>(std::clamp(std::floor(local.y / line_height), 0.f,
                                                           static_cast<float>(lines - 1)));
    return byte_on_line(line, local.x);
}

std::size_t TextEditor::previous_stop(std::size_t byte) const {
    std::size_t result = 0;
    for (const std::size_t stop : text_stops_) {
        if (stop >= byte) {
            break;
        }
        result = stop;
    }
    return result;
}

std::size_t TextEditor::next_stop(std::size_t byte) const {
    for (const std::size_t stop : text_stops_) {
        if (stop > byte) {
            return stop;
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
    return out_of_atomic(to_boundary(text_, byte), false);
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
    return out_of_atomic(to_boundary(text_, byte), true);
}

void TextEditor::select_word(std::size_t byte) {
    if (secure_) {
        anchor_ = 0;
        caret_ = text_.size();
        return;
    }
    // A chip selects as a whole.
    for (const Segment& segment : segments_) {
        if (segment.atomic && segment.text_begin <= byte && byte < segment.text_end) {
            anchor_ = segment.text_begin;
            caret_ = segment.text_end;
            return;
        }
    }
    // The run of word (or of non-word) characters around `byte`.
    const bool word = byte < text_.size() ? is_word_byte(text_[byte]) : byte > 0 && is_word_byte(text_[byte - 1]);
    std::size_t begin = byte;
    std::size_t end = byte;
    while (begin > 0 && is_word_byte(text_[begin - 1]) == word && text_[begin - 1] != '\n') {
        --begin;
    }
    while (end < text_.size() && is_word_byte(text_[end]) == word && text_[end] != '\n') {
        ++end;
    }
    anchor_ = out_of_atomic(to_boundary(text_, begin), false);
    caret_ = out_of_atomic(to_boundary(text_, end), true);
}

Size TextEditor::measure(ProposedSize proposal) const {
    // Takes the width it's offered (like SwiftUI); ideally, its text's.
    const float natural = std::max({natural_width_, placeholder_width_, kMinWidth}) + kCaretWidth;
    const float width = proposal.width.value_or(natural);
    const float line_height = shape_.line_height > 0 ? shape_.line_height : line_height_;
    if (!multiline()) {
        return {width, line_height};
    }
    // Grows a line at a time up to max_lines, then scrolls.
    const TextLayoutResult wrapped = layout_for(std::max(width - kCaretWidth, 1.f));
    const std::size_t lines = std::clamp<std::size_t>(wrapped.lines.size(), 1, static_cast<std::size_t>(max_lines_));
    return {width, line_height * static_cast<float>(lines)};
}

void TextEditor::arranged(Rect frame) {
    frame_ = frame;
    const float width = std::max(frame.width - kCaretWidth, 1.f);
    if (!layout_width_ || *layout_width_ != width) {
        relayout(width);
    }
    const float line_height = shape_.line_height > 0 ? shape_.line_height : line_height_;
    const CaretStop& caret = stop_at(to_shown(caret_) + (composing() ? composition_cursor_ : 0));
    if (multiline()) {
        // Keep the caret's line in sight when there are more lines than shown.
        scroll_x_ = 0;
        const float content = static_cast<float>(layout_.lines.size()) * line_height;
        const float top = static_cast<float>(caret.line) * line_height;
        if (top < scroll_y_) {
            scroll_y_ = top;
        } else if (top + line_height > scroll_y_ + frame.height) {
            scroll_y_ = top + line_height - frame.height;
        }
        scroll_y_ = std::clamp(scroll_y_, 0.f, std::max(content - frame.height, 0.f));
    } else {
        // Keep the caret in sight when the text is wider than the field.
        scroll_y_ = 0;
        const float room = std::max(frame.width - kCaretWidth, 0.f);
        if (layout_.width <= room) {
            scroll_x_ = 0;
        } else {
            if (caret.x - scroll_x_ < 0) {
                scroll_x_ = caret.x;
            } else if (caret.x - scroll_x_ > room) {
                scroll_x_ = caret.x - room;
            }
            scroll_x_ = std::clamp(scroll_x_, 0.f, layout_.width - room);
        }
    }
    // Tell the IME where we are (for its candidate list), when that changed.
    if (focused_ && services_) {
        const float cursor = caret.x - scroll_x_;
        const Rect area{frame.x, frame.y + static_cast<float>(caret.line) * line_height - scroll_y_, frame.width, line_height};
        if (!input_area_ || *input_area_ != area || input_cursor_ != cursor) {
            input_area_ = area;
            input_cursor_ = cursor;
            services_->set_text_input_area(area, cursor);
        }
    }
}

void TextEditor::paint(DisplayList& list, Rect frame) const {
    const float scale = list.scale > 0 ? list.scale : 1.f;
    const auto snap = [&](float value) { return std::round(value * scale) / scale; };
    const bool active = enabled();
    const Color tint = environment_.get<TintKey>();
    const Color foreground = active ? environment_.get<ForegroundKey>() : environment_.color(ThemeColor::tertiary);
    const float line_height = shape_.line_height > 0 ? shape_.line_height : line_height_;
    const Point origin{frame.x - scroll_x_, frame.y - scroll_y_};

    list.commands.push_back(PushClipCommand{Rect{frame.x - kCaretWidth, frame.y, frame.width + 2 * kCaretWidth, frame.height}, {}});

    if (focused_ && !composing() && caret_ != anchor_) {
        const std::size_t s0 = to_shown(selection_start());
        const std::size_t s1 = to_shown(selection_end());
        Color selection = tint;
        selection.a *= 0.35f;
        for (std::size_t li = 0; li < layout_.lines.size(); ++li) {
            const CaretStop* first = nullptr;
            const CaretStop* last = nullptr;
            for (const CaretStop& stop : stops_) {
                if (stop.line == li) {
                    first = first ? first : &stop;
                    last = &stop;
                }
            }
            if (!first || s1 <= first->byte || s0 > last->byte) {
                continue;
            }
            const auto x_at = [&](std::size_t byte) {
                float x = first->x;
                for (const CaretStop& stop : stops_) {
                    if (stop.line == li && stop.byte <= byte) {
                        x = stop.x;
                    }
                }
                return x;
            };
            const float x0 = x_at(std::max(s0, first->byte));
            float x1 = x_at(std::min(s1, last->byte));
            if (s1 > last->byte) {
                x1 += size_ * 0.3f; // a selected line break shows as a sliver
            }
            list.commands.push_back(RectCommand{{origin.x + x0, origin.y + static_cast<float>(li) * line_height, x1 - x0,
                                                 line_height},
                                                {}, ShapeStyle(selection), Color::clear, 0});
        }
    }

    if (face_) {
        if (text_.empty() && !composing()) {
            float pen = origin.x;
            const float baseline = snap(origin.y + ascent_);
            for (const ShapedGlyph& glyph : placeholder_shaped_.glyphs) {
                draw_glyph(list, text_system_->atlas, glyph.face ? *glyph.face : *face_, glyph, pen, baseline, size_,
                           environment_.color(ThemeColor::tertiary));
                pen += glyph.advance * size_;
            }
        } else {
            std::vector<DrawStyle> styles = styles_template_;
            for (std::size_t i = 0; i < styles.size(); ++i) {
                styles[i].color = i < style_colors_.size() && style_colors_[i] ? *style_colors_[i] : foreground;
            }
            draw_styled(list, text_system_->atlas, *face_, shape_, layout_, styles, origin, size_, scale);
        }
    }

    if (composing()) {
        // The IME's text is underlined until it's committed.
        const std::size_t at = to_shown(caret_);
        const CaretStop& start = stop_at(at);
        const CaretStop& end = stop_at(at + composition_.size());
        if (start.line == end.line) {
            const float y = origin.y + static_cast<float>(start.line) * line_height + shape_.ascent + 2;
            list.commands.push_back(RectCommand{{origin.x + start.x, snap(y), end.x - start.x, 1}, {},
                                                ShapeStyle(foreground), Color::clear, 0});
        }
    }

    if (focused_ && active && (caret_ == anchor_ || composing())) {
        const double elapsed = std::max(frame_time() - blink_start_, 0.0);
        const auto phase = static_cast<long long>(elapsed / kBlinkHalfPeriod);
        if (phase % 2 == 0) {
            const CaretStop& caret = stop_at(to_shown(caret_) + (composing() ? composition_cursor_ : 0));
            list.commands.push_back(RectCommand{{snap(origin.x + caret.x), origin.y + static_cast<float>(caret.line) * line_height,
                                                 kCaretWidth, line_height},
                                                {}, ShapeStyle(tint), Color::clear, 0});
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
    text_ = clean(std::move(value));
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
    text = clean(std::move(text));
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
        // The caret's line's ends (multiline Home/End).
        const auto line_end = [&](bool last) {
            const std::size_t line = line_of(caret_);
            std::size_t result = last ? 0 : text_.size();
            for (const CaretStop& stop : stops_) {
                if (stop.line == line) {
                    const std::size_t byte = to_text(stop.byte);
                    result = last ? std::max(result, byte) : std::min(result, byte);
                }
            }
            return result;
        };
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
        case Key::home: move_caret(multiline() && !primary ? line_end(false) : 0, shift); return handled;
        case Key::end: move_caret(multiline() && !primary ? line_end(true) : text_.size(), shift); return handled;
        case Key::up:
        case Key::down: {
            // A line up or down at the same x; past the first or last line, to the start or end.
            const std::size_t line = line_of(caret_);
            const bool up = event.key == Key::up;
            const std::size_t lines = std::max<std::size_t>(layout_.lines.size(), 1);
            if (!multiline() || (up && line == 0) || (!up && line + 1 >= lines)) {
                move_caret(up ? 0 : text_.size(), shift);
            } else {
                const float x = stop_at(to_shown(caret_)).x;
                move_caret(byte_on_line(up ? line - 1 : line + 1, x), shift);
            }
            return handled;
        }
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
            if (multiline() && shift) {
                replace_selection("\n", false); // Shift+Enter: a new line; Enter submits
                return handled;
            }
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
    const auto byte_under = [&] {
        return byte_at({event.location.x - frame_.x + scroll_x_, event.location.y - frame_.y + scroll_y_});
    };
    switch (event.kind) {
    case PointerEvent::Kind::hover: return {};
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
        // caret (the pointer handler does that next), and focus from code leaves it where it is.
        if (focus_ && focus_->navigated()) {
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
    if (key_filter_ && key_filter_(event)) {
        return {.consumed = true, .redraw = true};
    }
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
