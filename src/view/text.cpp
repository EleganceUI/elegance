// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The Text view and its element.

#include <elg/text.h>

#include "view/view_node.h"

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
    paragraphs_ = shape_text(*face_, content_);
    invalidate_layout();
}

void TextElement::set_content(std::string_view content) {
    content_ = content;
    if (face_) {
        paragraphs_ = shape_text(*face_, content_);
    }
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
    arranged_ = layout(proposal);
}

void TextElement::paint_impl(DisplayList& list) const {
    const Rect bounds = frame();
    const float scale = list.scale > 0 ? list.scale : 1.f;

    for (std::size_t i = 0; i < arranged_.lines.size(); ++i) {
        const TextLine& line = arranged_.lines[i];
        float pen = bounds.x + (bounds.width - line.width) * fraction(alignment_);
        // Baselines on whole device pixels keep text crisp.
        const float baseline =
            std::round((bounds.y + ascent_ + static_cast<float>(i) * line_height_) * scale) / scale;

        const auto emit = [&](const ShapedGlyph& glyph) {
            if (const AtlasGlyph* atlas = text_->atlas.find_or_add(*face_, glyph.id)) {
                const Rect quad{pen + (glyph.x_offset + atlas->left) * size_,
                                baseline - (glyph.y_offset + atlas->top) * size_,
                                (atlas->right - atlas->left) * size_, (atlas->top - atlas->bottom) * size_};
                const Rect uv{atlas->u0, atlas->v0, atlas->u1 - atlas->u0, atlas->v1 - atlas->v0};
                list.commands.push_back(GlyphCommand{quad, uv, color_, size_});
            }
            pen += glyph.advance * size_;
        };

        const ShapedParagraph& paragraph = paragraphs_[line.paragraph];
        for (std::size_t g = line.begin; g < line.end; ++g) {
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
