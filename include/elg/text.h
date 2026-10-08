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

#include <memory>
#include <string>

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

    ELG_API Text font(Font font) const;
    ELG_API Text font_weight(Weight weight) const;
    ELG_API Text bold() const;
    ELG_API Text foreground_style(Prop<Color> color) const;
    // Maximum number of lines; 0 for no limit.
    ELG_API Text line_limit(int lines) const;
    // How lines align when the text wraps.
    ELG_API Text multiline_text_alignment(TextAlignment alignment) const;

    // Internal.
    ELG_API explicit Text(std::shared_ptr<const detail::TextNode> node);
};

} // namespace elg

#endif // ELG_TEXT_H
