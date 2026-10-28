// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// StyledText: text in runs of different weights, colors, and highlights, with clickable runs.

#include "core/crash.h"
#include "text/styled_layout.h"
#include "text/text_system.h"
#include "view/accessibility.h"
#include "view/element.h"
#include "view/view_node.h"

#include <elg/environment.h>
#include <elg/text.h>

#include <cmath>

namespace elg::detail {

// A run's font: the base font, then the run's family, weight, and slant.
Font run_font(const Font& base, const RunStyle& style) {
    Font font = style.family.empty() ? base : Font::custom(style.family, base.size(), base.weight());
    if (style.weight) {
        font = font.weight(*style.weight);
    }
    if (style.italic) {
        font = font.italic();
    }
    return font;
}

namespace {

// A run's attachment (TextRun::attachment), placed where its placeholder glyph lands.
struct Attachment {
    std::size_t run = 0;
    std::unique_ptr<Element> element;
    bool shown = false; // on a laid-out line (not cut off by a line limit)
};

class StyledTextElement final : public Element, private EventHandler {
public:
    StyledTextElement(TextSystem& text, std::vector<TextRun> runs, std::vector<Attachment> attachments)
        : text_(&text), runs_(std::move(runs)), attachments_(std::move(attachments)) {
        for (const TextRun& run : runs_) {
            content_ += run.text;
            has_taps_ = has_taps_ || static_cast<bool>(run.on_tap);
        }
        for (Attachment& attachment : attachments_) {
            adopt(*attachment.element);
        }
    }

    void for_each_child(const std::function<void(Element&)>& visit) override {
        for (Attachment& attachment : attachments_) {
            visit(*attachment.element);
        }
    }

    void set_font(const Font& font) {
        font_ = font;
        reshape();
    }
    void set_color(Color color) {
        color_ = color;
        styles_ = draw_styles();
    }

    EventHandler* event_handler() noexcept override { return has_taps_ ? this : nullptr; }

    void accessibility(AccessibilityInfo& out) const override {
        if (content_.empty()) {
            out.hidden = true;
            return;
        }
        out.role = AccessRole::label;
        out.label = content_;
    }

protected:
    Size measure(ProposedSize proposal) override {
        const TextLayoutResult result = layout(proposal);
        return {result.width, result.height};
    }
    void arrange(Rect bounds, ProposedSize proposal) override {
        if (arranged_for_ != proposal) {
            arranged_ = layout(proposal);
            arranged_for_ = proposal;
        }
        place_attachments(bounds);
    }
    void paint_impl(DisplayList& list) const override {
        if (!base_) {
            return;
        }
        const Rect bounds = frame();
        draw_styled(list, text_->atlas, *base_, shape_, arranged_, styles_, {bounds.x, bounds.y}, font_.size(),
                    list.scale > 0 ? list.scale : 1.f);
        for (const Attachment& attachment : attachments_) {
            if (attachment.shown) {
                attachment.element->paint(list);
            }
        }
    }
    bool hit_impl(Point point, std::vector<Element*>& path) override {
        // An attachment under the point (its tooltip, say) first; the text's taps still apply.
        for (Attachment& attachment : attachments_) {
            if (attachment.shown && attachment.element->hit_test(point, path)) {
                return true;
            }
        }
        const Rect bounds = frame();
        return point.x >= bounds.x && point.y >= bounds.y && point.x < bounds.max_x() && point.y < bounds.max_y();
    }
    void on_layout_invalidated() noexcept override { arranged_for_.reset(); }

private:
    EventResult pointer(const PointerEvent& event, Rect) override {
        if (event.button != MouseButton::left) {
            return {};
        }
        if (event.kind == PointerEvent::Kind::down) {
            pressed_ = run_at(event.location);
            return {.consumed = pressed_.has_value(), .cancellable = true};
        }
        if (event.kind == PointerEvent::Kind::up && pressed_) {
            const std::optional<std::size_t> released = run_at(event.location);
            const std::optional<std::size_t> pressed = std::exchange(pressed_, std::nullopt);
            if (released == pressed) {
                runs_[*pressed].on_tap();
            }
            return {.consumed = true};
        }
        if (event.kind == PointerEvent::Kind::cancel) {
            pressed_.reset();
        }
        return {};
    }

    // The clickable run under `point`, if any.
    std::optional<std::size_t> run_at(Point point) const {
        const Rect bounds = frame();
        if (shape_.line_height <= 0 || point.y < bounds.y) {
            return std::nullopt;
        }
        const auto li = static_cast<std::size_t>((point.y - bounds.y) / shape_.line_height);
        if (li >= arranged_.lines.size()) {
            return std::nullopt;
        }
        const TextLine& line = arranged_.lines[li];
        const ShapedParagraph& paragraph = shape_.paragraphs[line.paragraph];
        float x = bounds.x;
        for (std::size_t g = line.begin; g < line.end; ++g) {
            const float advance = paragraph.glyphs[g].advance * font_.size();
            if (point.x >= x && point.x < x + advance) {
                const std::uint32_t style = shape_.styles[line.paragraph][g];
                if (style > 0 && runs_[style - 1].on_tap) {
                    return style - 1;
                }
                return std::nullopt;
            }
            x += advance;
        }
        return std::nullopt;
    }

    // Each attachment centered in its placeholder's box on its line.
    void place_attachments(Rect bounds) {
        if (attachments_.empty()) {
            return;
        }
        for (Attachment& attachment : attachments_) {
            attachment.shown = false;
        }
        for (std::size_t li = 0; li < arranged_.lines.size(); ++li) {
            const TextLine& line = arranged_.lines[li];
            const ShapedParagraph& paragraph = shape_.paragraphs[line.paragraph];
            float x = bounds.x;
            for (std::size_t g = line.begin; g < line.end; ++g) {
                const ShapedGlyph& glyph = paragraph.glyphs[g];
                const float advance = glyph.advance * font_.size();
                if (glyph.placeholder) {
                    const std::uint32_t style = shape_.styles[line.paragraph][g];
                    for (Attachment& attachment : attachments_) {
                        if (attachment.run + 1 == style) {
                            const float side = advance;
                            const float y = bounds.y + static_cast<float>(li) * shape_.line_height +
                                            (shape_.line_height - side) / 2;
                            attachment.element->place({x, y}, ProposedSize{side, side});
                            attachment.shown = true;
                        }
                    }
                }
                x += advance;
            }
        }
    }

    // Style 0: plain; style i + 1: run i.
    std::vector<DrawStyle> draw_styles() const {
        std::vector<DrawStyle> styles{{.color = color_}};
        for (const TextRun& run : runs_) {
            styles.push_back({.color = run.style.color.value_or(color_),
                              .background = run.style.background,
                              .underline = run.style.underline,
                              .strikethrough = run.style.strikethrough});
        }
        return styles;
    }

    void reshape() {
        base_ = &text_->fonts.resolve(font_);
        std::vector<StyledRunSpec> specs;
        std::size_t at = 0;
        for (std::size_t i = 0; i < runs_.size(); ++i) {
            const TextRun& run = runs_[i];
            specs.push_back({.begin = at,
                             .end = at + run.text.size(),
                             .face = &text_->fonts.resolve(run_font(font_, run.style)),
                             .style = static_cast<std::uint32_t>(i + 1),
                             .keep_together = run.style.keep_together || run.style.background.has_value(),
                             .box = run.attachment ? run.attachment_size : 0.f});
            at += run.text.size();
        }
        shape_ = shape_styled(content_, specs, *base_, font_.size(), text_->fonts);
        styles_ = draw_styles();
        // Lines that match the new shape right away: a view on its way out is painted without
        // being laid out again (see TextElement::relayout_now).
        if (arranged_for_) {
            arranged_ = layout(*arranged_for_);
        } else {
            arranged_ = {};
        }
        invalidate_layout();
    }

    TextLayoutResult layout(ProposedSize proposal) const {
        return layout_text(shape_.paragraphs, {.font_size = font_.size(),
                                               .line_height = shape_.line_height,
                                               .max_width = proposal.width,
                                               .max_height = proposal.height});
    }

    TextSystem* text_;
    std::vector<TextRun> runs_;
    std::vector<Attachment> attachments_;
    std::string content_;
    bool has_taps_ = false;
    Font font_ = Font::system(15);
    Color color_ = Color::black;
    const FontFace* base_ = nullptr;
    StyledShape shape_;
    std::vector<DrawStyle> styles_;
    TextLayoutResult arranged_;
    std::optional<ProposedSize> arranged_for_;
    std::optional<std::size_t> pressed_;
};

class StyledTextNode final : public ViewNode {
public:
    explicit StyledTextNode(std::vector<TextRun> runs) : runs_(std::move(runs)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        ELG_ASSERT(context.text, "lowering StyledText without a text system");
        const EnvironmentValues environment = context.environment;
        std::vector<Attachment> attachments;
        for (std::size_t i = 0; i < runs_.size(); ++i) {
            if (runs_[i].attachment) {
                attachments.push_back({i, detail::lower(*runs_[i].attachment, context)});
            }
        }
        auto element = std::make_unique<StyledTextElement>(*context.text, runs_, std::move(attachments));
        StyledTextElement* const target = element.get();
        // Color first: the font's reshape draws with it.
        detail::bind(Prop<Color>([environment] { return environment.get<ForegroundKey>(); }), context,
                     [target](Color color, const Animation*) { target->set_color(color); });
        detail::bind(Prop<Font>([environment] { return environment.get<FontKey>(); }), context,
                     [target](const Font& font) { target->set_font(font); });
        return element;
    }

private:
    std::vector<TextRun> runs_;
};

} // namespace

} // namespace elg::detail

namespace elg {

View StyledText(std::vector<TextRun> runs) {
    return detail::ViewAccess::make(std::make_shared<const detail::StyledTextNode>(std::move(runs)));
}

} // namespace elg
