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

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elg {

/// What an Image shows: an icon, an image file, or image data. Set exactly one of the first three.
struct ImageSource {
    /// An icon: "<set>.<icon-name>[.<variant>]", or "<icon-name>[.<variant>]" in the default set.
    std::string system_name;
    /// A raster image file: PNG, JPEG, GIF (first frame), BMP, TIFF, ICO; WebP, HEIF, and AVIF
    /// where the system has the codecs. Photos are turned upright by their EXIF orientation.
    std::filesystem::path file;
    /// Encoded image data in memory, in any of those formats (copied).
    std::span<const unsigned char> data;
    /// Raster images: pixels per point, e.g. 2 for @2x artwork. 0 or less counts as 1.
    float scale = 1;
};

/// An icon or a raster image. Raster images are drawn at their own size (pixels / scale) unless
/// made .resizable(). They decode in the background and appear once ready; their size is known
/// right away, so layout doesn't jump. A file that can't be read or decoded logs a warning and
/// shows nothing.
///
/// @param source What to show.
/// @return The image.
/// @throws elg::UsageError For an unknown icon (naming the closest matches) or icon set, or when
///   `source` doesn't set exactly one of `system_name`, `file` and `data`.
ELG_API View Image(const ImageSource& source);
/// An icon: Image("gear") is Image({.system_name = "gear"}).
///
/// @param system_name "<set>.<icon-name>[.<variant>]", or "<icon-name>[.<variant>]" in the
///   default set.
/// @return The icon.
/// @throws elg::UsageError For an unknown icon (naming the closest matches) or icon set, or an
///   empty name.
ELG_API View Image(std::string_view system_name);

// --- AsyncImage: an image from the network ---

/// Where an AsyncImage is, for its content function.
struct AsyncImagePhase {
    /// The phases, in order: loading, then success or failure.
    enum class Kind { loading, success, failure };
    Kind kind = Kind::loading; ///< which phase it is
    View image;                ///< success: the image (an Image, so .resizable() applies)
    std::string error;         ///< failure: why
};

/// How an AsyncImage shows its image and caches it.
struct AsyncImageOptions {
    float scale = 1; ///< pixels per point
    /// Resizable, keeping its aspect ratio this way (unset: at its own size, like Image). Not used
    /// with a content function, whose views size the image.
    std::optional<ContentMode> content_mode;
    /// false: kept in memory only, never in the disk cache (for private images whose server
    /// doesn't send Cache-Control: no-store).
    bool disk_cache = true;
};

/// Loads an image over HTTP(S) when first shown (design.md §13.3): an indeterminate Progress while
/// loading, the image once it's here, a placeholder icon (with the error as its tooltip) if it
/// fails. Downloads are cached in memory, so lists scrolling back don't fetch again, and on disk
/// for as long as the server allows (set_image_cache). A view that goes away cancels its download.
///
/// @code
/// AsyncImage(url, {.content_mode = ContentMode::fill}).frame(48, 48).clip_shape(Circle())
/// @endcode
///
/// @param url The image's http:// or https:// URL.
/// @param options Its scale, sizing and caching.
/// @return The view for the current phase.
ELG_API View AsyncImage(std::string url, AsyncImageOptions options = {});
/// The same, with your own view for each phase:
///
/// @code
/// AsyncImage(url, [](const AsyncImagePhase& phase) {
///     return phase.kind == AsyncImagePhase::Kind::success ? phase.image.resizable() : View(Color::gray);
/// })
/// @endcode
///
/// @param url The image's http:// or https:// URL.
/// @param content Builds the view for a phase; called again when the phase changes.
/// @param options Its scale and caching (`content_mode` isn't used).
/// @return The view `content` builds for the current phase.
/// @throws elg::UsageError If `content` is empty.
ELG_API View AsyncImage(std::string url, std::function<View(const AsyncImagePhase&)> content,
                        AsyncImageOptions options = {});

/// AsyncImage's disk cache: downloads kept between runs while Cache-Control (or Expires) says
/// they're fresh, then revalidated with the server; a stale copy is shown when the server can't
/// be reached. The least recently used go first past the limit. ELG_IMAGE_CACHE=off in the
/// environment turns it off.
struct ImageCacheOptions {
    bool disk = true;                        ///< false: no disk cache, only the one in memory
    std::uint64_t disk_limit = 256ull << 20; ///< bytes
    /// Empty: <the user's cache folder>/Elegance/<this executable's name>/Images.
    std::filesystem::path directory;
};
/// Configures AsyncImage's disk cache. Downloads already under way use the cache they started
/// with. UI thread.
///
/// @param options Whether there's a disk cache, its size limit and its folder.
ELG_API void set_image_cache(ImageCacheOptions options);
/// Forgets every downloaded image, in memory and on disk (when signing out, say). Downloads under
/// way finish without being cached. UI thread.
ELG_API void clear_image_cache();

/// An icon followed by a title, spaced for buttons, menus, and lists.
///
/// @param title The text after the icon.
/// @param icon The icon (or image), as for Image().
/// @return The label.
/// @throws elg::UsageError For an unknown icon, as Image() does.
ELG_API View Label(Prop<std::string> title, const ImageSource& icon);

// --- Icon sets ---

/// A font file, or font data in memory (copied when registered). Converts implicitly from a path
/// or bytes, so `.font = "brand-icons.ttf"` works.
class IconFont {
public:
    IconFont() = default;
    /// A font file.
    ///
    /// @param file The font file's path; it must exist when the set is registered.
    IconFont(std::filesystem::path file) : file_(std::move(file)) {}
    /// A font file.
    ///
    /// @param file The font file's path; it must exist when the set is registered.
    IconFont(const char* file) : file_(file) {}
    /// A font file.
    ///
    /// @param file The font file's path; it must exist when the set is registered.
    IconFont(const std::string& file) : file_(file) {}
    /// Font data in memory.
    ///
    /// @param data The font file's bytes. Not copied here: they must stay valid until
    ///   register_icon_set() returns, which copies them.
    IconFont(std::span<const unsigned char> data) : data_(data) {}

    /// The file.
    ///
    /// @return The path; empty for font data.
    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }
    /// The font data.
    ///
    /// @return The bytes; empty for a file.
    [[nodiscard]] std::span<const unsigned char> data() const noexcept { return data_; }
    /// Whether it has neither a file nor data.
    ///
    /// @return true for a default-constructed IconFont.
    [[nodiscard]] bool empty() const noexcept { return file_.empty() && data_.empty(); }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::filesystem::path file_;
    std::span<const unsigned char> data_;
    ELG_PRIVATE_MEMBERS_END
};

/// A variant of an icon set ("<set>.<icon>.<variant>"): its font, and its own name map when its
/// codepoints differ from the set's (empty: the set's).
struct IconVariant {
    std::string name;            ///< the `<variant>` in icon names; not empty, no dots
    IconFont font;               ///< its font
    std::filesystem::path names; ///< its own name map, in a format IconSetOptions::names takes
    std::string_view names_data; ///< or the map's text (read by register_icon_set()); used over `names`
};

/// An icon set for register_icon_set(): its fonts and the map from icon names to their glyphs.
struct IconSetOptions {
    IconFont font;                                    ///< the regular weight
    std::vector<std::pair<Weight, IconFont>> weights; ///< more weights (for .font_weight())
    std::vector<IconVariant> variants;                ///< e.g. {"fill", "brand-fill.ttf"}
    /// The name -> codepoint map, as the set ships it. Formats are recognized by content:
    ///  - CSS from icon webfonts: `.prefix-name:before { content: "\e900"; }` (a prefix shared by
    ///    every name, like "ph-", is removed). An `:after` rule adds a second layer drawn over the
    ///    first, and `opacity:` sets a layer's opacity (two-tone icons);
    ///  - JSON: {"name": "e900", ...} or {"name": 59648, ...};
    ///  - Material-style codepoints text: one "name e900" per line.
    /// The same names apply to every weight and variant.
    std::filesystem::path names;
    std::string_view names_data; ///< or the map's text (read during the call); used over `names`
    bool replace = false;        ///< replace a set registered under the same name
};

/// Makes "<name>.<icon>" available to Image.
///
/// @code
/// register_icon_set("brand", {.font = "brand-icons.ttf", .names = "brand-icons.css"});
/// @endcode
///
/// @param name The set's name, the prefix of its icons' names; not empty, no dots.
/// @param options Its fonts and name map; `font` and `names` (or `names_data`) are required.
/// @throws elg::UsageError If the name is taken (unless `replace`) or invalid, or the options are
///   incomplete.
/// @throws elg::ResourceError If a file can't be read, or the name map has no icons.
ELG_API void register_icon_set(std::string name, const IconSetOptions& options);

/// Chooses the icon set for names without a set prefix ("phosphor" unless changed). Names are resolved
/// when an Image is made, so an unregistered set makes those Images throw then.
///
/// @param name A registered set's name.
ELG_API void set_default_icon_set(std::string name);

} // namespace elg

#endif // ELG_IMAGE_H
