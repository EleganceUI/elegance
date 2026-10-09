// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_IMAGE_H
#define ELG_IMAGE_H

// Images and icon sets (design.md §13.2).
//
//   Image({.system_name = "phosphor.heart"})        // set "phosphor", icon "heart"
//   Image({.system_name = "phosphor.heart.fill"})   // a variant of the set
//   Image("gear")                                    // the default set (Phosphor)
//   Image("trash").foreground_style(ThemeColor::danger).font(Font::title2)
//   Label("Search", {.system_name = "magnifying-glass"})
//
// Icons are glyphs of their set's font: they take the environment's font size, weight (sets with
// weights), and foreground color, and sit on the text baseline. Names are the set's own (Phosphor:
// https://phosphoricons.com).

#include <elg/export.h>
#include <elg/font.h>
#include <elg/view.h>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elg {

// What an Image shows. Set exactly one field.
struct ImageSource {
    // An icon: "<set>.<icon-name>[.<variant>]", or "<icon-name>[.<variant>]" in the default set.
    std::string system_name;
};

// Throws elg::UsageError for an unknown icon (naming the closest matches).
ELG_API View Image(ImageSource source);
ELG_API View Image(std::string_view system_name);

// An icon followed by a title, spaced for buttons, menus, and lists.
ELG_API View Label(std::string title, ImageSource icon);

// --- Icon sets ---

// A font file, or font data in memory (copied when registered).
class IconFont {
public:
    IconFont() = default;
    IconFont(std::filesystem::path file) : file_(std::move(file)) {}
    IconFont(const char* file) : file_(file) {}
    IconFont(const std::string& file) : file_(file) {}
    IconFont(std::span<const unsigned char> data) : data_(data) {}

    const std::filesystem::path& file() const noexcept { return file_; }
    std::span<const unsigned char> data() const noexcept { return data_; }
    bool empty() const noexcept { return file_.empty() && data_.empty(); }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::filesystem::path file_;
    std::span<const unsigned char> data_;
    ELG_PRIVATE_MEMBERS_END
};

struct IconSetOptions {
    IconFont font;                                           // the regular weight
    std::vector<std::pair<Weight, IconFont>> weights;        // more weights (for .font_weight())
    std::vector<std::pair<std::string, IconFont>> variants;  // "<set>.<icon>.<variant>" fonts
    // The name -> codepoint map, as the set ships it. Formats are recognized by content:
    //  - CSS from icon webfonts: `.prefix-name:before { content: "\e900"; }` (a prefix shared by
    //    every name, like "ph-", is removed);
    //  - JSON: {"name": "e900", ...} or {"name": 59648, ...};
    //  - Material-style codepoints text: one "name e900" per line.
    // The same names apply to every weight and variant.
    std::filesystem::path names;
    std::string_view names_data; // or the map's text (read during the call)
    bool replace = false;        // replace a set registered under the same name
};

// Makes "<name>.<icon>" available to Image. Throws elg::UsageError if the name is taken (unless
// `replace`) or the options are incomplete, elg::ResourceError if a file can't be read.
//   register_icon_set("brand", {.font = "brand-icons.ttf", .names = "brand-icons.css"});
ELG_API void register_icon_set(std::string name, IconSetOptions options);

// The set for names without a set prefix ("phosphor" unless changed).
ELG_API void set_default_icon_set(std::string name);

} // namespace elg

#endif // ELG_IMAGE_H
