// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The Text view and its element.

#include <elg/text.h>

#include "text/glyph_draw.h"
#include "view/view_node.h"

#include <algorithm>
#include <cmath>

namespace elg {

using detail::TextNode;
using detail::TextStyle;

namespace {

const TextNode& node_of(const Text& text) {
    // A Text is only ever constructed from a TextNode.
    return static_cast<const TextNode&>(*detail::ViewAccess::node(text));
}

template <class F>
Text with_style(const Text& text, F&& change) {
    const TextNode& node = node_of(text);
    TextStyle style = node.style();
    change(style);
    return Text(std::make_shared<const TextNode>(node.content(), std::move(style)));
}

} // namespace

Text::Text(Prop<std::string> content)
    : Text(std::make_shared<const TextNode>(std::move(content), TextStyle{})) {}

Text::Text(std::shared_ptr<const TextNode> node) : View(detail::ViewAccess::make(std::move(node))) {}

Text Text::font(Font font) const {
    return with_style(*this, [&](TextStyle& style) { style.font = std::move(font); });
}

Text Text::font_weight(Weight weight) const {
    return with_style(*this, [&](TextStyle& style) { style.weight = weight; });
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
    paragraphs_ = shape_text(*face_, content_, &text_->fonts);
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
}

void TextElement::set_content(std::string_view content) {
    if (content == content_) {
        return;
    }
    content_ = content;
    snap_next_layout();
    if (face_) {
        paragraphs_ = shape_text(*face_, content_, &text_->fonts);
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
    }
}

void TextElement::paint_impl(DisplayList& list) const {
    const Rect bounds = frame();
    const float scale = list.scale > 0 ? list.scale : 1.f;
    const Color color = color_.current();
    if (color_.animating()) {
        list.animating = true;
    }

    for (std::size_t i = 0; i < arranged_.lines.size(); ++i) {
        const TextLine& line = arranged_.lines[i];
        float pen = bounds.x + (bounds.width - line.width) * fraction(alignment_);
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

} // namespace elg::detail
