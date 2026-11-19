// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The Text view and its element.

#include <elg/text.h>

#include "core/numeric.h"
#include "core/utf8.h"
#include "text/text_boundaries.h"
#include "view/focus.h"
#include "view/glyph_draw.h"
#include "view/styled_draw.h"
#include "view/view_node.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace elg {

using detail::TextNode;
using detail::TextStyle;

namespace {

const TextNode& node_of(const Text& text) {
    // A Text is only ever constructed from a TextNode.
    return static_cast<const TextNode&>(*detail::ViewAccess::node(text));
}

template <class F>
Text with_style(const Text& text, const F& change) {
    const TextNode& node = node_of(text);
    TextStyle style = node.style();
    change(style);
    return detail::ViewAccess::text(std::make_shared<const TextNode>(node.content(), std::move(style)));
}

} // namespace

Text::Text(Prop<std::string> content)
    : Text(std::make_shared<const TextNode>(std::move(content), TextStyle{})) {}

Text::Text(std::shared_ptr<const TextNode> node) : View(detail::ViewAccess::make(std::move(node))) {}

Text Text::font(Prop<Font> font) const {
    return with_style(*this, [&](TextStyle& style) { style.font = std::move(font); });
}

Text Text::font_weight(Prop<Weight> weight) const {
    return with_style(*this, [&](TextStyle& style) { style.weight = std::move(weight); });
}

Text Text::bold() const {
    return font_weight(Weight::bold);
}

Text Text::foreground_style(Prop<Color> color) const {
    return with_style(*this, [&](TextStyle& style) { style.color = std::move(color); });
}

Text Text::line_limit(int lines) const {
    return with_style(*this, [&](TextStyle& style) { style.line_limit = lines > 0 ? lines : 0; });
}

Text Text::multiline_text_alignment(TextAlignment alignment) const {
    return with_style(*this, [&](TextStyle& style) { style.alignment = alignment; });
}

Text Text::selectable(Prop<bool> selectable) const {
    return with_style(*this, [&](TextStyle& style) { style.selectable = std::move(selectable); });
}

} // namespace elg

namespace elg::detail {

namespace {

float fraction(TextAlignment alignment) {
    switch (alignment) {
    case TextAlignment::leading: return 0;
    case TextAlignment::center: return 0.5f;
    case TextAlignment::trailing: return 1;
    }
    return 0;
}

ShapedParagraph shape_ellipsis(const FontFace& face) {
    std::vector<ShapedParagraph> shaped = shape_text(face, "…");
    for (const ShapedGlyph& glyph : shaped.front().glyphs) {
        if (glyph.id == 0) {
            return shape_text(face, "...").front(); // the font has no ellipsis character
        }
    }
    return shaped.front();
}

} // namespace

TextElement::TextElement(TextSystem& text, int line_limit, TextAlignment alignment)
    : text_(&text), line_limit_(line_limit), alignment_(alignment) {}

TextElement::~TextElement() {
    if (window_) {
        window_->text_selection_cleared(this);
    }
}

void TextElement::reshape() {
    paragraphs_ = shape_text(*face_, content_, &text_->fonts);
    // shape_text() splits at '\n', one paragraph after each.
    paragraph_starts_.assign(1, 0);
    for (std::size_t i = 0; i < content_.size(); ++i) {
        if (content_[i] == '\n') {
            paragraph_starts_.push_back(i + 1);
        }
    }
    stops_.reset();
}

void TextElement::set_font(const Font& font) {
    face_ = &text_->fonts.resolve(font);
    size_ = font.size();
    ascent_ = face_->ascender() * size_;
    line_height_ = (face_->ascender() - face_->descender() + face_->line_gap()) * size_;
    ellipsis_ = shape_ellipsis(*face_);
    ellipsis_width_ = 0;
    for (const ShapedGlyph& glyph : ellipsis_.glyphs) {
        ellipsis_width_ += glyph.advance * size_;
    }
    reshape();
    relayout_now();
    invalidate_layout();
}

void TextElement::relayout_now() {
    // The lines must always index the paragraphs they're drawn from: a view on its way out (an
    // exit transition) is painted without being laid out again, and old lines over new, shorter
    // text would read past its glyphs.
    if (arranged_for_) {
        arranged_ = layout(*arranged_for_);
    } else if (!arranged_.lines.empty()) {
        arranged_ = {};
    }
    stops_.reset();
}

void TextElement::set_content(std::string_view content) {
    std::string valid = valid_utf8(content);
    if (valid == content_) {
        return;
    }
    clear_selection(); // of text that isn't there any more
    content_ = std::move(valid);
    snap_next_layout();
    if (face_) {
        reshape();
    }
    relayout_now();
    invalidate_layout();
}

TextLayoutResult TextElement::layout(ProposedSize proposal) const {
    return layout_text(paragraphs_, {
                                        .font_size = size_,
                                        .line_height = line_height_,
                                        .ellipsis_width = ellipsis_width_,
                                        .max_width = proposal.width,
                                        .max_height = proposal.height,
                                        .line_limit = line_limit_,
                                    });
}

Size TextElement::measure(ProposedSize proposal) {
    const TextLayoutResult result = layout(proposal);
    return {result.width, result.height};
}

void TextElement::arrange(Rect, ProposedSize proposal) {
    // Every frame lays out the whole tree: line breaking only reruns when something changed.
    if (arranged_for_ != proposal) {
        arranged_ = layout(proposal);
        arranged_for_ = proposal;
        stops_.reset();
    }
}

void TextElement::paint_impl(DisplayList& list) const {
    const Rect bounds = frame();
    const float scale = list.scale > 0 ? list.scale : 1.f;
    const Color color = color_.current();
    if (color_.animating()) {
        list.animating = true;
    }

    if (selectable_ && anchor_ != end_) {
        paint_selection(list, bounds);
    }
    for (std::size_t i = 0; i < arranged_.lines.size(); ++i) {
        const TextLine& line = arranged_.lines[i];
        float pen = bounds.x + line_offset(i, bounds.width);
        // Baselines on whole device pixels keep text crisp.
        const float baseline =
            std::round((bounds.y + ascent_ + static_cast<float>(i) * line_height_) * scale) / scale;

        const auto emit = [&](const ShapedGlyph& glyph) {
            draw_glyph(list, text_->atlas, glyph.face ? *glyph.face : *face_, glyph, pen, baseline, size_, color);
            pen += glyph.advance * size_;
        };

        if (line.paragraph >= paragraphs_.size()) {
            break;
        }
        const ShapedParagraph& paragraph = paragraphs_[line.paragraph];
        for (std::size_t g = line.begin; g < std::min(line.end, paragraph.glyphs.size()); ++g) {
            emit(paragraph.glyphs[g]);
        }
        if (line.ellipsis) {
            for (const ShapedGlyph& glyph : ellipsis_.glyphs) {
                emit(glyph);
            }
        }
    }
}

// --- Selectable text (Text::selectable()) ---

void TextElement::make_selectable(FocusManager* window) {
    can_select_ = true;
    window_ = window;
}

void TextElement::set_selectable(bool selectable) {
    if (selectable == selectable_) {
        return;
    }
    selectable_ = selectable;
    if (!selectable) {
        clear_selection();
    }
}

void TextElement::set_selection_tint(Color tint) {
    tint.a *= kSelectionOpacity;
    if (tint == selection_color_) {
        return;
    }
    selection_color_ = tint;
    if (anchor_ != end_) {
        invalidate_paint();
    }
}

void TextElement::set_enabled(bool enabled) {
    enabled_ = enabled;
    if (!enabled) {
        clear_selection();
    }
}

void TextElement::clear_selection() {
    dragging_ = false;
    const bool shown = anchor_ != end_;
    anchor_ = 0;
    end_ = 0;
    if (window_) {
        window_->text_selection_cleared(this);
    }
    if (shown) {
        invalidate_paint();
    }
}

std::string TextElement::selected_text() const {
    return content_.substr(selection_start(), selection_end() - selection_start());
}

void TextElement::select_all() {
    const std::vector<CaretStop>& all = stops();
    select(all.front().byte, all.back().byte);
    invalidate_paint();
}

EventHandler* TextElement::event_handler() noexcept {
    if (!can_select_) {
        return nullptr;
    }
    return enabled_ ? static_cast<EventHandler*>(this) : &disabled_;
}

EventHandler* TextElement::disabled_handler() noexcept {
    return can_select_ && !enabled_ ? &disabled_ : nullptr;
}

const std::vector<CaretStop>& TextElement::stops() const {
    if (!stops_) {
        stops_ = caret_stops(paragraphs_, paragraph_starts_, arranged_, size_, content_.size());
    }
    return *stops_;
}

float TextElement::line_offset(std::size_t line, float width) const {
    return line < arranged_.lines.size() ? (width - arranged_.lines[line].width) * fraction(alignment_) : 0;
}

std::size_t TextElement::position_at(Point local) const {
    // The line drawn at the point's height (the first or the last above or below the text), then
    // the stop drawn nearest it on that line.
    const std::size_t lines = std::max<std::size_t>(arranged_.lines.size(), 1);
    const float height = line_height_ > 0 ? line_height_ : 1;
    const auto line = clamped_cast<std::size_t>(std::floor(local.y / height), 0, lines - 1);
    const float x = local.x - line_offset(line, frame().width);
    const CaretStop* best = nullptr;
    for (const CaretStop& stop : stops()) {
        if (stop.line == line && (!best || std::abs(stop.x - x) < std::abs(best->x - x))) {
            best = &stop;
        }
    }
    return best ? best->byte : 0;
}

std::size_t TextElement::snapped(std::size_t byte, bool up) const {
    // Stops come in order; past the last one (text a line limit leaves out) is the last one.
    const std::vector<CaretStop>& all = stops();
    if (up) {
        const auto found = std::ranges::find_if(all, [&](const CaretStop& stop) { return stop.byte >= byte; });
        return found != all.end() ? found->byte : all.back().byte;
    }
    std::size_t result = all.front().byte;
    for (const CaretStop& stop : all) {
        if (stop.byte > byte) {
            break;
        }
        result = stop.byte;
    }
    return result;
}

void TextElement::select(std::size_t anchor, std::size_t end) {
    anchor_ = anchor;
    end_ = end;
    if (window_) {
        window_->select_text(this);
    }
}

EventResult TextElement::pointer(const PointerEvent& event, Rect frame) {
    if (!selectable_ || event.button != MouseButton::left) {
        return {};
    }
    const Point local{event.location.x - frame.x, event.location.y - frame.y};
    switch (event.kind) {
    case PointerEvent::Kind::hover: return {};
    case PointerEvent::Kind::down: {
        // Taken for good (not cancellable): gestures around it don't get the press unless they
        // have high priority, as with a text field.
        const std::size_t at = position_at(local);
        dragging_ = false;
        if (event.click_count >= 3) {
            const auto [begin, end] = paragraph_around(content_, at);
            select(snapped(begin, false), snapped(end, true));
        } else if (event.click_count == 2) {
            const auto [begin, end] = word_around(content_, at);
            select(snapped(begin, false), snapped(end, true));
        } else {
            const bool extends = has(event.modifiers, KeyModifiers::shift) &&
                                 (window_ ? window_->selected_text() == this : anchor_ != end_);
            select(extends ? anchor_ : at, at);
            dragging_ = true;
        }
        return {.consumed = true, .redraw = true};
    }
    case PointerEvent::Kind::move: {
        if (!dragging_) {
            return {};
        }
        const std::size_t at = position_at(local);
        if (at == end_) {
            return {.consumed = true};
        }
        end_ = at;
        return {.consumed = true, .redraw = true};
    }
    case PointerEvent::Kind::up:
    case PointerEvent::Kind::cancel: dragging_ = false; return {.consumed = true};
    }
    return {};
}

PointerStyle TextElement::pointer_style(Point) const {
    return selectable_ && !yields_pointer_style_ ? PointerStyle::text : PointerStyle::automatic;
}

void TextElement::paint_selection(DisplayList& list, Rect bounds) const {
    const std::size_t s0 = selection_start();
    const std::size_t s1 = selection_end();
    const std::vector<CaretStop>& all = stops();
    for (std::size_t li = 0; li < arranged_.lines.size(); ++li) {
        const CaretStop* first = nullptr;
        const CaretStop* last = nullptr;
        for (const CaretStop& stop : all) {
            if (stop.line == li) {
                first = first ? first : &stop;
                last = &stop;
            }
        }
        if (!first) {
            continue;
        }
        const std::size_t from = std::max(s0, first->byte);
        const std::size_t to = std::min(s1, last->byte);
        // A line break the selection takes in shows as a sliver past the line's end.
        const bool line_break = last->byte < content_.size() && content_[last->byte] == '\n' && s0 <= last->byte &&
                                s1 > last->byte;
        if (from >= to && !line_break) {
            continue;
        }
        const auto x_at = [&](std::size_t byte) {
            float x = first->x;
            for (const CaretStop& stop : all) {
                if (stop.line == li && stop.byte <= byte) {
                    x = stop.x;
                }
            }
            return x;
        };
        const float x0 = from < to ? x_at(from) : last->x;
        float x1 = from < to ? x_at(to) : last->x;
        if (line_break) {
            x1 += size_ * 0.3f;
        }
        const float x = bounds.x + line_offset(li, bounds.width);
        list.commands.emplace_back(RectCommand{{x + x0, bounds.y + static_cast<float>(li) * line_height_, x1 - x0,
                                                line_height_},
                                               {}, ShapeStyle(selection_color_), Color::clear, 0});
    }
}

} // namespace elg::detail
