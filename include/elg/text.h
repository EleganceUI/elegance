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

/// How the lines of wrapped text align within the text's frame.
enum class TextAlignment { leading, center, trailing };

/// A run of text in one font and color. It wraps at word boundaries to the proposed width and
/// truncates with an ellipsis when it runs out of lines (line_limit or the proposed height).
/// Without a font or color of its own, it uses the environment's (.font(), .foreground_style()).
///
/// Text modifiers return Text, so they chain until a generic modifier turns it into a View:
///
/// @code
/// Text("Hello").font(Font::title).bold().foreground_style(Color::white).padding(8)
/// @endcode
///
/// Members are exported individually, like View's (see view.h).
class Text : public View {
public:
    /// Static or reactive: `Text("Hello")` or `Text([=] { return std::format("{} items", count()); })`
    ///
    /// @param content The text, UTF-8. Newlines start new paragraphs.
    ELG_API explicit Text(Prop<std::string> content);

    /// Sets the font, in place of the environment's. Throws elg::ResourceError when the text is
    /// mounted if the font's family isn't registered.
    ///
    /// @param font The font, static or reactive.
    /// @return This text, in `font`.
    [[nodiscard]] ELG_API Text font(Prop<Font> font) const;
    /// Overrides the weight of the font, whether it's the text's own or the environment's.
    ///
    /// @param weight The weight, static or reactive.
    /// @return This text, in `weight`.
    [[nodiscard]] ELG_API Text font_weight(Prop<Weight> weight) const;
    /// Same as `.font_weight(Weight::bold)`.
    ///
    /// @return This text, bold.
    [[nodiscard]] ELG_API Text bold() const;
    /// Sets the color, in place of the environment's foreground.
    ///
    /// @param color The color, static or reactive.
    /// @return This text, in `color`.
    [[nodiscard]] ELG_API Text foreground_style(Prop<Color> color) const;
    /// Limits the number of lines; text past the last line is cut off with an ellipsis.
    ///
    /// @param lines Maximum number of lines; 0 (or less) for no limit.
    /// @return This text, at most `lines` lines high.
    [[nodiscard]] ELG_API Text line_limit(int lines) const;
    /// How lines align when the text wraps.
    ///
    /// @param alignment Where each line goes within the text's frame. The default is leading.
    /// @return This text, with its lines aligned.
    [[nodiscard]] ELG_API Text multiline_text_alignment(TextAlignment alignment) const;

    /// Internal.
    ///
    /// @param node The text's description.
    ELG_API explicit Text(std::shared_ptr<const detail::TextNode> node);
};

// --- Styled text: runs in different weights, colors, and highlights ---

/// How a run of text looks; unset parts come from the environment (.font, .foreground_style).
struct RunStyle {
    std::optional<Weight> weight;    ///< none: the font's weight
    bool italic = false;             ///< the italic face (the system font's; registered families stay upright)
    std::string family;              ///< a registered family (e.g. monospace for code); empty: the font's
    std::optional<Color> color;      ///< none: the environment's foreground
    std::optional<Color> background; ///< a rounded highlight behind it (mentions, inline code)
    bool underline = false;          ///< a 1-point line under the run, in its color
    bool strikethrough = false;      ///< a 1-point line through the run, in its color
    bool keep_together = false;      ///< never broken across lines (implied by a background)
};

/// One run of a StyledText: text in one style, optionally clickable or shown as a view.
struct TextRun {
    std::string text; ///< The run's text, UTF-8.
    RunStyle style;   ///< How it looks.
    /// clicking the run (links, mentions); none: not clickable. Called on the UI thread when the left
    /// button is released over the run it went down on.
    std::function<void()> on_tap;
    /// Shown in the line instead of the text (which stays the accessible label): a view in a
    /// square `attachment_size` em wide and high, centered on the line, e.g. a custom emoji.
    std::optional<View> attachment;
    float attachment_size = 1.25f; ///< The attachment's width and height, in ems of the font size.
};

/// Text made of runs, wrapping like Text (newlines in runs start paragraphs). It takes its font and
/// color from the environment (.font(), .foreground_style()); each run's style changes them from
/// there.
///
/// @code
/// StyledText({{"Hey "}, {"@Ana", {.color = blurple, .background = blurple_tint}, open_profile},
///             {" look at "}, {"this", {.weight = Weight::bold}},
///             {":blob:", {}, {}, AsyncImage(blob_url, {.content_mode = ContentMode::fit})}})
/// @endcode
///
/// @param runs The runs, in reading order.
/// @return The text view.
ELG_API View StyledText(std::vector<TextRun> runs);

/// A styled range of a TextField's text (TextFieldOptions::styler), in bytes. With a `label`, the
/// range shows as the label and is edited as one unit: the caret goes around it and Backspace
/// deletes it whole (mentions shown as chips while the text keeps "<@123>"). An offset inside a
/// character moves back to its start; spans that are then empty or overlap an earlier one are skipped.
struct TextSpan {
    std::size_t begin = 0;            ///< Byte offset of the range's start.
    std::size_t end = 0;              ///< Byte offset just past the range's end.
    RunStyle style;                   ///< How the range looks.
    std::optional<std::string> label; ///< Shown in place of the range; none: the range's own text.
};

} // namespace elg

#endif // ELG_TEXT_H
