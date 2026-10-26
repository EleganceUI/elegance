// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_IMAGE_H
#define ELG_IMAGE_H

// Images (design.md §13.3) and icon sets (§13.2).
//
//   Image({.file = "assets/photo.jpg"})             // a raster image, at its own size
//   Image({.file = "photo.jpg"}).resizable().aspect_ratio(ContentMode::fill).frame(160, 100).clipped()
//   Image({.data = embedded_png})                   // encoded image data in memory
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
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elg {

// What an Image shows: an icon, an image file, or image data. Set exactly one of the first three.
struct ImageSource {
    // An icon: "<set>.<icon-name>[.<variant>]", or "<icon-name>[.<variant>]" in the default set.
    std::string system_name;
    // A raster image file: PNG, JPEG, GIF (first frame), BMP, TIFF, ICO; WebP, HEIF, and AVIF
    // where the system has the codecs. Photos are turned upright by their EXIF orientation.
    std::filesystem::path file;
    // Encoded image data in memory, in any of those formats (copied).
    std::span<const unsigned char> data;
    // Raster images: pixels per point, e.g. 2 for @2x artwork.
    float scale = 1;
};

// Raster images are drawn at their own size (pixels / scale) unless made .resizable(). They
// decode in the background and appear once ready; their size is known right away, so layout
// doesn't jump. A file that can't be read or decoded logs a warning and shows nothing.
// Throws elg::UsageError for an unknown icon (naming the closest matches).
ELG_API View Image(ImageSource source);
ELG_API View Image(std::string_view system_name);

// --- AsyncImage: an image from the network ---

// Where an AsyncImage is, for its content function.
struct AsyncImagePhase {
    enum class Kind { loading, success, failure };
    Kind kind = Kind::loading;
    View image;        // success: the image (an Image, so .resizable() applies)
    std::string error; // failure: why
};

struct AsyncImageOptions {
    float scale = 1; // pixels per point
    // Resizable, keeping its aspect ratio this way (unset: at its own size, like Image).
    std::optional<ContentMode> content_mode;
};

// Loads an image over HTTP(S) when first shown (design.md §13.3): a spinner while loading, the
// image once it's here, a placeholder icon if it fails. Downloads are cached in memory, so lists
// scrolling back don't fetch again.
//   AsyncImage(url, {.content_mode = ContentMode::fill}).frame(48, 48).clip_shape(Circle())
ELG_API View AsyncImage(std::string url, AsyncImageOptions options = {});
// The same, with your own view for each phase:
//   AsyncImage(url, [](const AsyncImagePhase& phase) {
//       return phase.kind == AsyncImagePhase::Kind::success ? phase.image.resizable() : View(Color::gray);
//   })
ELG_API View AsyncImage(std::string url, std::function<View(const AsyncImagePhase&)> content, float scale = 1);

// An icon followed by a title, spaced for buttons, menus, and lists.
ELG_API View Label(Prop<std::string> title, ImageSource icon);

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

// A variant of an icon set ("<set>.<icon>.<variant>"): its font, and its own name map when its
// codepoints differ from the set's (empty: the set's).
struct IconVariant {
    std::string name;
    IconFont font;
    std::filesystem::path names;
    std::string_view names_data;
};

struct IconSetOptions {
    IconFont font;                                    // the regular weight
    std::vector<std::pair<Weight, IconFont>> weights; // more weights (for .font_weight())
    std::vector<IconVariant> variants;                // e.g. {"fill", "brand-fill.ttf"}
    // The name -> codepoint map, as the set ships it. Formats are recognized by content:
    //  - CSS from icon webfonts: `.prefix-name:before { content: "\e900"; }` (a prefix shared by
    //    every name, like "ph-", is removed). An `:after` rule adds a second layer drawn over the
    //    first, and `opacity:` sets a layer's opacity (two-tone icons);
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
