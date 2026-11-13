// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_FONT_H
#define ELG_FONT_H

#include <elg/export.h>

#include <filesystem>
#include <span>
#include <string>

namespace elg {

/// The named weights, and any between them: Weight{550}, or static_cast<Weight>(w) from a value in
/// 1...1000. Variable fonts (macOS's system font, a variable file passed to register_font) draw
/// each weight exactly; other fonts use their closest weight.
enum class Weight {
    ultra_light = 100,
    thin = 200,
    light = 300,
    regular = 400,
    medium = 500,
    semibold = 600,
    bold = 700,
    heavy = 800,
    black = 900,
};

/// A font description: family, size in points, and weight. Resolved to a font file when text is
/// mounted; an unknown family throws elg::ResourceError then.
class Font {
public:
    /// The platform UI font (Segoe UI on Windows, SF on macOS, fontconfig's sans-serif on Linux).
    ///
    /// @param size The size in points.
    /// @param weight The weight.
    /// @return The font.
    static Font system(float size, Weight weight = Weight::regular) { return Font({}, size, weight); }
    /// A family registered with register_font().
    ///
    /// @param family The name it was registered under.
    /// @param size The size in points.
    /// @param weight The weight; the family's closest registered weight is used.
    /// @return The font.
    static Font custom(std::string family, float size, Weight weight = Weight::regular) {
        return Font(std::move(family), size, weight);
    }

    /// @param weight The new weight.
    /// @return This font in `weight`.
    [[nodiscard]] Font weight(Weight weight) const { return Font(family_, size_, weight, italic_); }
    /// @return This font in Weight::bold.
    [[nodiscard]] Font bold() const { return weight(Weight::bold); }
    /// @param size The new size in points.
    /// @return This font at `size`.
    [[nodiscard]] Font with_size(float size) const { return Font(family_, size, weight_, italic_); }
    /// The italic face (the system font's; registered families stay upright).
    ///
    /// @param italic false for the upright face.
    /// @return This font, italic or upright.
    [[nodiscard]] Font italic(bool italic = true) const { return Font(family_, size_, weight_, italic); }

    /// @return The family name; empty for the system font.
    [[nodiscard]] const std::string& family() const noexcept { return family_; }
    /// @return The size in points.
    [[nodiscard]] float size() const noexcept { return size_; }
    /// @return The weight.
    [[nodiscard]] Weight weight() const noexcept { return weight_; }
    /// @return Whether italic() asked for the italic face.
    [[nodiscard]] bool is_italic() const noexcept { return italic_; }

    // Semantic text styles (sizes are tentative until Dynamic Type-style scaling, design.md §9.5).
    static const Font large_title; ///< 30 points, bold.
    static const Font title;       ///< 24 points, semibold.
    static const Font title2;      ///< 20 points, semibold.
    static const Font title3;      ///< 17 points, semibold.
    static const Font headline;    ///< 15 points, semibold.
    static const Font body;        ///< 15 points, regular: the default.
    static const Font callout;     ///< 14 points, regular.
    static const Font subheadline; ///< 13 points, regular.
    static const Font footnote;    ///< 12 points, regular.
    static const Font caption;     ///< 12 points, regular.
    static const Font caption2;    ///< 11 points, regular.

    friend bool operator==(const Font&, const Font&) = default;

private:
    Font(std::string family, float size, Weight weight, bool italic = false)
        : family_(std::move(family)), size_(size), weight_(weight), italic_(italic) {}

    std::string family_;
    float size_ = 15;
    Weight weight_ = Weight::regular;
    bool italic_ = false;
};

inline const Font Font::large_title = Font::system(30, Weight::bold);
inline const Font Font::title = Font::system(24, Weight::semibold);
inline const Font Font::title2 = Font::system(20, Weight::semibold);
inline const Font Font::title3 = Font::system(17, Weight::semibold);
inline const Font Font::headline = Font::system(15, Weight::semibold);
inline const Font Font::body = Font::system(15);
inline const Font Font::callout = Font::system(14);
inline const Font Font::subheadline = Font::system(13);
inline const Font Font::footnote = Font::system(12);
inline const Font Font::caption = Font::system(12);
inline const Font Font::caption2 = Font::system(11);

/// Makes a font file available as Font::custom(family, ...). Register each weight's file
/// separately; the closest registered weight is used. A variable font (one file with a weight
/// axis) needs registering once: it draws every weight. Thread-safe.
///
/// @param family The name Font::custom() uses; not empty.
/// @param file The font file. It's read when text first uses it.
/// @param weight The weight of the face in `file`.
/// @throws UsageError When `family` is empty.
/// @throws ResourceError When `file` isn't a regular file.
ELG_API void register_font(std::string family, std::filesystem::path file, Weight weight = Weight::regular);
/// The same from font data in memory (e.g. embedded in the app); the bytes are copied.
///
/// @param family The name Font::custom() uses; not empty.
/// @param data The font file's contents.
/// @param weight The weight of the face in `data`.
/// @throws UsageError When `family` is empty.
/// @throws ResourceError When `data` is empty.
ELG_API void register_font(std::string family, std::span<const unsigned char> data, Weight weight = Weight::regular);

/// Draws emoji with this color font (COLR/CPAL, e.g. Twemoji) before the system's, in every text
/// view: characters it lacks still fall back to the system emoji font. Fonts registered earlier
/// win. Thread-safe; register before the text is first shaped.
///
/// @param file The font file.
/// @throws ResourceError When `file` isn't a regular file.
ELG_API void register_emoji_font(std::filesystem::path file);

} // namespace elg

#endif // ELG_FONT_H
