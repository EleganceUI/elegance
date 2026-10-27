// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_TEXT_H
#define ELG_TEXT_H

#include <elg/color.h>
#include <elg/export.h>
#include <elg/font.h>
#include <elg/prop.h>
#include <elg/view.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace elg {

namespace detail {
class TextNode;
}

enum class TextAlignment { leading, center, trailing };

// A run of text in one font and color. It wraps at word boundaries to the proposed width and
// truncates with an ellipsis when it runs out of lines (line_limit or the proposed height).
// Without a font or color of its own, it uses the environment's (.font(), .foreground_style()).
//
// Text modifiers return Text, so they chain until a generic modifier turns it into a View:
//   Text("Hello").font(Font::title).bold().foreground_style(Color::white).padding(8)
// Members are exported individually, like View's (see view.h).
class Text : public View {
public:
    // Static or reactive: Text("Hello") or Text([=] { return std::format("{} items", count()); })
    ELG_API explicit Text(Prop<std::string> content);

    ELG_API Text font(Prop<Font> font) const;
    ELG_API Text font_weight(Prop<Weight> weight) const;
    ELG_API Text bold() const;
    ELG_API Text foreground_style(Prop<Color> color) const;
    // Maximum number of lines; 0 for no limit.
    ELG_API Text line_limit(int lines) const;
    // How lines align when the text wraps.
    ELG_API Text multiline_text_alignment(TextAlignment alignment) const;

    // Internal.
    ELG_API explicit Text(std::shared_ptr<const detail::TextNode> node);
};

// --- Styled text: runs in different weights, colors, and highlights ---

// How a run of text looks; unset parts come from the environment (.font, .foreground_style).
struct RunStyle {
    std::optional<Weight> weight;
    bool italic = false;
    std::string family;              // a registered family (e.g. monospace for code); empty: the font's
    std::optional<Color> color;
    std::optional<Color> background; // a rounded highlight behind it (mentions, inline code)
    bool underline = false;
    bool strikethrough = false;
    bool keep_together = false;      // never broken across lines (implied by a background)
};

struct TextRun {
    std::string text;
    RunStyle style;
    std::function<void()> on_tap; // clicking the run (links, mentions); none: not clickable
    // Shown in the line instead of the text (which stays the accessible label): a view in a
    // square `attachment_size` em wide and high, centered on the line, e.g. a custom emoji.
    std::optional<View> attachment;
    float attachment_size = 1.25f;
};

// Text made of runs, wrapping like Text (newlines in runs start paragraphs):
//   StyledText({{"Hey "}, {"@Ana", {.color = blurple, .background = blurple_tint}, open_profile},
//               {" look at "}, {"this", {.weight = Weight::bold}},
//               {":blob:", {}, {}, AsyncImage(blob_url, {.content_mode = ContentMode::fit})}})
ELG_API View StyledText(std::vector<TextRun> runs);

// A styled range of a TextField's text (TextFieldOptions::styler), in bytes. With a `label`, the
// range shows as the label and is edited as one unit: the caret goes around it and Backspace
// deletes it whole (mentions shown as chips while the text keeps "<@123>").
struct TextSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
    RunStyle style;
    std::optional<std::string> label;
};

} // namespace elg

#endif // ELG_TEXT_H
