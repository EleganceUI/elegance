// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_H
#define ELG_VIEW_H

#include <elg/color.h>
#include <elg/events.h>
#include <elg/export.h>
#include <elg/geometry.h>
#include <elg/gestures.h>
#include <elg/gradient.h>
#include <elg/prop.h>

#include <concepts>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace elg {

/// Identifies a ForEach item across updates: an integer (enums too) or a string.
using ItemKey = std::variant<std::int64_t, std::string>;

namespace detail {
class ViewNode;
struct ViewAccess;

// For View::animation: a function that reads `source` (a tracked read) and returns whether it
// changed since the previous call (false on the first call).
template <class Source>
std::function<bool()> change_watcher(Source source) {
    auto read = [source = std::move(source)] {
        if constexpr (std::invocable<const Source&>) {
            return std::invoke(source);
        } else {
            return source;
        }
    };
    using Value = std::decay_t<decltype(read())>;
    return [read = std::move(read), last = std::make_shared<std::optional<Value>>()]() {
        Value value = read();
        bool changed = last->has_value();
        if constexpr (std::equality_comparable<Value>) {
            changed = changed && !(**last == value);
        }
        *last = std::move(value);
        return changed;
    };
}

template <class K>
ItemKey to_item_key(const K& key) {
    if constexpr (std::is_integral_v<K> || std::is_enum_v<K>) {
        return static_cast<std::int64_t>(key);
    } else {
        static_assert(std::is_convertible_v<const K&, std::string_view>,
                      "ForEach keys must be integers, enums, or strings");
        return std::string(std::string_view(key));
    }
}

// The key a ForEach made with to_item_key, back as the app's type.
template <class K>
K from_item_key(const ItemKey& key) {
    if constexpr (std::is_integral_v<K> || std::is_enum_v<K>) {
        return static_cast<K>(std::get<std::int64_t>(key));
    } else {
        static_assert(std::is_constructible_v<K, const std::string&>,
                      "selection keys must be integers, enums, or strings (std::string, not std::string_view)");
        return K(std::get<std::string>(key));
    }
}

// View::selection, type-erased for the library.
struct SelectionModel {
    bool multiple = false;
    std::function<std::vector<ItemKey>()> get;     // the selected keys; a tracked read
    std::function<bool(const ItemKey&)> contains;  // a tracked read
    std::function<void(std::vector<ItemKey>)> set; // a single selection takes the first, or none
    std::function<void(const ItemKey&)> activate;  // SelectionOptions::on_activate; may be empty
};
} // namespace detail

class Animation;
class ButtonStyle;
class ToggleStyle;
class CheckboxStyle;
class NumberInputStyle;
class ProgressStyle;
class NavigationStackStyle;
class TextFieldStyle;
class PopoverStyle;
class TooltipStyle;
class MenuStyle;
class PickerStyle;
class SliderStyle;
class RowStyle;
enum class Weight;
template <class T>
class Binding;
class Font;
class Transition;
class Modifier;
class Shape;
class ShapeStyle;
struct Theme;
enum class ColorScheme;
template <class Key>
class EnvironmentKeyModifier;
template <class Key>
class LayoutValueModifier;

/// An unbounded length: as a proposal it asks for the maximum size, and as a frame's maximum
/// (`.frame({.max_width = infinity})`) it fills whatever is proposed.
inline constexpr float infinity = std::numeric_limits<float>::infinity();

/// A layout direction: along x (horizontal) or along y (vertical).
enum class Axis { horizontal, vertical };

/// Where content sits across a width: at the leading edge, centered, or at the trailing edge.
enum class HorizontalAlignment { leading, center, trailing };
/// Where content sits along a height: at the top, centered, or at the bottom.
enum class VerticalAlignment { top, center, bottom };

/// Where content sits in a larger space, on both axes. The default centers it; the constants
/// name the nine positions (Alignment::top_leading, ...).
struct Alignment {
    HorizontalAlignment horizontal = HorizontalAlignment::center; ///< Across the width.
    VerticalAlignment vertical = VerticalAlignment::center;       ///< Along the height.

    static const Alignment center;          ///< Centered on both axes.
    static const Alignment top;             ///< Centered across the top edge.
    static const Alignment bottom;          ///< Centered across the bottom edge.
    static const Alignment leading;         ///< Centered along the leading edge.
    static const Alignment trailing;        ///< Centered along the trailing edge.
    static const Alignment top_leading;     ///< In the top leading corner.
    static const Alignment top_trailing;    ///< In the top trailing corner.
    static const Alignment bottom_leading;  ///< In the bottom leading corner.
    static const Alignment bottom_trailing; ///< In the bottom trailing corner.

    friend constexpr bool operator==(const Alignment&, const Alignment&) = default;
};

inline constexpr Alignment Alignment::center{};
inline constexpr Alignment Alignment::top{HorizontalAlignment::center, VerticalAlignment::top};
inline constexpr Alignment Alignment::bottom{HorizontalAlignment::center, VerticalAlignment::bottom};
inline constexpr Alignment Alignment::leading{HorizontalAlignment::leading, VerticalAlignment::center};
inline constexpr Alignment Alignment::trailing{HorizontalAlignment::trailing, VerticalAlignment::center};
inline constexpr Alignment Alignment::top_leading{HorizontalAlignment::leading, VerticalAlignment::top};
inline constexpr Alignment Alignment::top_trailing{HorizontalAlignment::trailing, VerticalAlignment::top};
inline constexpr Alignment Alignment::bottom_leading{HorizontalAlignment::leading, VerticalAlignment::bottom};
inline constexpr Alignment Alignment::bottom_trailing{HorizontalAlignment::trailing, VerticalAlignment::bottom};

/// A size offered by a parent during layout (design.md §7). Each dimension is optional:
/// nullopt asks for the ideal size, 0 for the minimum, and infinity for the maximum.
struct ProposedSize {
    std::optional<float> width;  ///< The offered width in points; nullopt asks for the ideal width.
    std::optional<float> height; ///< The offered height in points; nullopt asks for the ideal height.

    /// @return A proposal of nothing on either axis: it asks for the ideal size.
    static constexpr ProposedSize unspecified() noexcept { return {}; }
    /// @return A proposal of 0 by 0: it asks for the minimum size.
    static constexpr ProposedSize zero() noexcept { return {0.f, 0.f}; }
    /// @return A proposal of infinity by infinity: it asks for the maximum size.
    static constexpr ProposedSize infinite() noexcept { return {infinity, infinity}; }

    /// Fills in the dimensions this proposal leaves open.
    ///
    /// @param fallback The lengths to use where a dimension is nullopt.
    /// @return This proposal as a Size, with `fallback`'s length on each unspecified axis.
    [[nodiscard]] constexpr Size replacing_unspecified(Size fallback) const noexcept {
        return {width.value_or(fallback.width), height.value_or(fallback.height)};
    }

    friend constexpr bool operator==(const ProposedSize&, const ProposedSize&) = default;
};

/// What View::selection does besides selecting.
///
/// @tparam K The selection's key type: the ForEach's.
template <class K>
struct SelectionOptions {
    /// Enter, or a double click on a row: opens what the row stands for. Called with the row's
    /// key; may be empty.
    std::function<void(const K&)> on_activate;
};

/// What a view is to screen readers, when the automatic choice isn't right (accessibility_role()).
enum class AccessibilityRole {
    group,
    label,
    heading,
    button,
    link,
    image,
    toggle,
    checkbox,
    slider,
    progress,
    text_field,
    list,
    list_item,
    tab,
    menu_item,
};

/// What a view does with things dragged in from other apps (on_drop()). Handlers get what was
/// dropped and where (in the view's coordinates). A drop goes to one view: the innermost enabled
/// one under the pointer with a handler for it (on top, among siblings), whose handler runs.
struct DropOptions {
    /// Takes dropped files, by path. Empty: the view doesn't take files.
    std::function<void(const std::vector<std::filesystem::path>& files, Point location)> files;
    /// Takes dropped text. Empty: the view doesn't take text.
    std::function<void(const std::string& text, Point location)> text;
    /// Set while a drag the view can take is over it (for a highlight): called with true when one
    /// comes over the view and false when it leaves or is dropped. May be empty.
    std::function<void(bool targeted)> targeted;
};

/// Where floating content (View::floating()) goes, by the view it's attached to (its anchor).
enum class FloatingEdge { bottom, top, trailing, leading };
/// Across that edge: lined up with the anchor's start (leading or top), center, or end.
enum class FloatingAlignment { start, center, end };

/// Where floating content goes, relative to the view it's attached to (its anchor).
struct FloatingPlacement {
    /// The anchor's side it opens on. It flips to the opposite side when there isn't room.
    FloatingEdge edge = FloatingEdge::bottom;
    /// Where it lines up along that edge: with the anchor's start, center or end.
    FloatingAlignment alignment = FloatingAlignment::start;
    float spacing = 0; ///< Points between the anchor and the content (negative: overlapping).
    Point offset{};    ///< Then moved by this, in points.
    /// Instead of an edge: the content's top-leading corner here, in the anchor's coordinates.
    std::optional<Point> position;
};

/// Where floating content may go.
enum class FloatingBoundary {
    /// In a popup window of its own: it can extend past the app's window, and stays on the
    /// display. Where the platform constrains popup windows (Wayland), the system has the last word.
    screen,
    /// Drawn inside the app's window, above its content, and kept within it.
    window,
    /// Drawn inside the nearest view around it marked .floating_container() (else the window):
    /// above that view's content, under views outside it, kept within it and clipped to it, and
    /// moving with it as it scrolls. Esc and outside clicks anywhere in the window still close it.
    container,
};

/// What closes floating content (and sets its binding back to false).
struct FloatingDismiss {
    /// A press anywhere outside it. While open, it then takes the pointer: presses elsewhere only
    /// close it. false: the rest of the window stays usable.
    bool outside_click = true;
    bool escape = true; ///< Esc, unless something focused handles it first.
};

/// How popup content opens and closes: `transition` run with `animation` (elg/animation.h).
///
/// @code
/// PopupMotion(Transition::opacity().combined(Transition::scale(0.96f, Alignment::top)),
///             Animation::ease_out(0.15))
/// @endcode
class ELG_API PopupMotion {
public:
    /// Content that enters and leaves with `transition`, timed by `animation`.
    ///
    /// @param transition How the content enters and leaves; copied.
    /// @param animation The timing of both; copied.
    PopupMotion(const Transition& transition, const Animation& animation);
    /// At once, with no animation (the built-in styles' default).
    ///
    /// @return A motion that doesn't animate: transition() and animation() are nullptr.
    static PopupMotion none() { return PopupMotion(); }

    /// @return The transition, or nullptr for none().
    [[nodiscard]] const Transition* transition() const noexcept { return transition_.get(); }
    /// @return The animation, or nullptr for none().
    [[nodiscard]] const Animation* animation() const noexcept { return animation_.get(); }
    /// @return Whether there's a transition to animate (false for none()).
    [[nodiscard]] bool animates() const noexcept { return transition_ && animation_; }

private:
    PopupMotion() = default;
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<const Transition> transition_;
    std::shared_ptr<const Animation> animation_;
    ELG_PRIVATE_MEMBERS_END
};

/// Where floating content (View::floating()) goes, how far it may go, what closes it, and how it
/// opens and closes.
struct FloatingOptions {
    FloatingPlacement placement;                          ///< By default below the view, at its start.
    FloatingBoundary boundary = FloatingBoundary::screen; ///< By default a popup window of its own.
    FloatingDismiss dismiss;                              ///< By default outside clicks and Esc.
    std::optional<PopupMotion> motion;                    ///< nullopt: none.
};

/// Where a tooltip (View::tooltip()) goes, when it shows, and how it opens and closes.
struct TooltipOptions {
    FloatingPlacement placement{.spacing = 4};            ///< Below the view by default.
    FloatingBoundary boundary = FloatingBoundary::screen; ///< By default a popup window of its own.
    double delay = 0.6;                ///< Seconds the pointer rests on the view before it shows.
    std::optional<PopupMotion> motion; ///< nullopt: the TooltipStyle's.
};

/// Where a popover (View::popover()) goes, how far it may go, and how it looks and moves.
struct PopoverOptions {
    /// Centered below the view by default.
    FloatingPlacement placement{.alignment = FloatingAlignment::center, .spacing = 4};
    FloatingBoundary boundary = FloatingBoundary::screen; ///< By default a popup window of its own.
    Prop<bool> padded = true; ///< false: the content reaches the panel's edges (images, lists).
    std::optional<PopupMotion> motion; ///< nullopt: the PopoverStyle's.
};

/// How content with an aspect ratio fills space (aspect_ratio()).
enum class ContentMode {
    fit,  ///< All of it shows: as large as fits.
    fill, ///< It covers the space: as small as covers it (overflowing on one axis; clip it).
};

/// Space on each edge of a rectangle, in points (padding()).
struct EdgeInsets {
    float top = 0;      ///< Space at the top.
    float leading = 0;  ///< Space at the leading edge.
    float bottom = 0;   ///< Space at the bottom.
    float trailing = 0; ///< Space at the trailing edge.

    /// The same space on every edge.
    ///
    /// @param length Space on every edge, in points.
    /// @return Insets of `length` on all four edges.
    static constexpr EdgeInsets all(float length) noexcept { return {length, length, length, length}; }
    /// The same space on both sides, and the same at the top and bottom.
    ///
    /// @param horizontal Space on the leading and trailing edges, in points.
    /// @param vertical Space on the top and bottom edges, in points.
    /// @return Insets of `horizontal` on the sides and `vertical` at the top and bottom.
    static constexpr EdgeInsets symmetric(float horizontal, float vertical) noexcept {
        return {vertical, horizontal, vertical, horizontal};
    }
};

/// A set of a rectangle's edges, combined with `|`: `Edges::top | Edges::leading`.
enum class Edges : unsigned {
    top = 1,
    leading = 2,
    bottom = 4,
    trailing = 8,
    horizontal = leading | trailing, ///< Leading and trailing.
    vertical = top | bottom,         ///< Top and bottom.
    all = top | leading | bottom | trailing,
};

/// The union of two sets of edges.
///
/// @param a Edges.
/// @param b More edges.
/// @return The edges in either.
constexpr Edges operator|(Edges a, Edges b) noexcept {
    return static_cast<Edges>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

/// Whether a set of edges includes an edge.
///
/// @param set The edges to look in.
/// @param edge The edge to look for. For a combination, any of its edges counts.
/// @return Whether `set` includes `edge`.
constexpr bool contains(Edges set, Edges edge) noexcept {
    return (static_cast<unsigned>(set) & static_cast<unsigned>(edge)) != 0;
}

/// Adjustments to what .backdrop_blur() shows through, after the blur. Each is reactive and
/// animatable.
struct BackdropOptions {
    Prop<float> saturation = 1.f; ///< 0 gray, 1 unchanged, above 1 more vivid.
    Prop<float> brightness = 0.f; ///< Added to each channel: -1 black ... 0 unchanged ... 1 white.
    Prop<float> noise = 0.f;      ///< Grain, 0...1, so smooth gradients don't band.
};

/// A drop shadow (design.md §9.4): `radius` is the blur radius in points, (x, y) the offset.
/// `spread` grows (or, negative, shrinks) the shape before blurring; it applies to single shapes
/// (rects, rounded rects, capsules, circles), as in Figma, and is ignored for other content.
/// Stack shadows by chaining: .shadow({.radius = 2, .y = 1}).shadow({.radius = 24, .y = 12}).
struct Shadow {
    Color color = Color::black.opacity(0.33f); ///< 33% black by default.
    float radius = 8;                          ///< Blur radius in points.
    float x = 0;                               ///< Horizontal offset in points (positive: right).
    float y = 0;                               ///< Vertical offset in points (positive: down).
    float spread = 0;                          ///< Points to grow the shape by before blurring.
};

/// How a view's pixels combine with what's behind them (design.md §9.4): the W3C compositing
/// modes, as in CSS mix-blend-mode, Figma, and SwiftUI. Blending reads the window behind the view
/// (or, inside an offscreen group, what the group drew behind it).
enum class BlendMode {
    normal,
    // Darken
    multiply,
    darken,
    color_burn,
    plus_darker,
    // Lighten
    screen,
    lighten,
    color_dodge,
    plus_lighter,
    // Contrast
    overlay,
    soft_light,
    hard_light,
    // Inversion
    difference,
    exclusion,
    // Component
    hue,
    saturation,
    color,
    luminosity,
};

/// An optional length in points, for option structs. Unlike std::optional<float>, it takes any
/// number ({.spacing = 12}, {.width = 12.5}) without narrowing warnings.
class OptionalLength {
public:
    constexpr OptionalLength() noexcept = default;
    /// No length.
    constexpr OptionalLength(std::nullopt_t) noexcept {}
    /// The length `length` holds, if any.
    ///
    /// @param length A length in points, or nullopt for none.
    constexpr OptionalLength(std::optional<float> length) noexcept : value_(length) {}
    /// A length given as any number but a bool, converted to float.
    ///
    /// @param length A length in points.
    template <class T>
        requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
    constexpr OptionalLength(T length) noexcept : value_(static_cast<float>(length)) {}

    /// @return Whether there's a length.
    [[nodiscard]] constexpr bool has_value() const noexcept { return value_.has_value(); }
    /// Whether there's a length.
    constexpr explicit operator bool() const noexcept { return value_.has_value(); }
    /// The length; there must be one (has_value()).
    ///
    /// @return The length in points.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): std::optional's precondition, callers check has_value()
    constexpr float operator*() const noexcept { return *value_; }
    /// The length, if there's one.
    ///
    /// @param fallback The length to use when there's none.
    /// @return The length, or `fallback` when there's none.
    [[nodiscard]] constexpr float value_or(float fallback) const noexcept { return value_.value_or(fallback); }
    /// The length, or nullopt when there's none.
    constexpr operator std::optional<float>() const noexcept { return value_; }

private:
    std::optional<float> value_;
};

/// .frame({.width = 100}): fixed dimensions; unset ones follow the content.
struct FrameOptions {
    OptionalLength width;  ///< The frame's width in points. Unset: the content's.
    OptionalLength height; ///< The frame's height in points. Unset: the content's.
    Alignment alignment = Alignment::center; ///< Where the content sits when its size differs.
};

/// .frame({.max_width = infinity}): a size range; the frame takes the proposed size, clamped.
/// A bound that isn't set comes from the content.
struct FlexibleFrameOptions {
    OptionalLength min_width;    ///< The least width, in points.
    OptionalLength ideal_width;  ///< The width when the parent proposes none. Unset: the content's.
    OptionalLength max_width;    ///< The most width, in points; `infinity` fills the proposed width.
    OptionalLength min_height;   ///< The least height, in points.
    OptionalLength ideal_height; ///< The height when the parent proposes none. Unset: the content's.
    OptionalLength max_height;   ///< The most height, in points; `infinity` fills the proposed height.
    Alignment alignment = Alignment::center; ///< Where the content sits when its size differs.
};

/// A description of UI: an immutable, cheaply copyable value. Nothing is created until a view
/// is mounted in a window (design.md §9.2).
///
/// Every modifier method is sugar for View::modifier() with a public modifier type
/// (design.md §9.4), e.g. padding(8) == modifier(PaddingModifier(EdgeInsets::all(8))).
///
/// Members are exported one by one rather than the whole class: a dllimport class can't have
/// members defined outside the DLL, and user extensions (below) are exactly that.
class View {
public:
    /// An empty view: takes no space and draws nothing.
    ELG_API View() noexcept;
    /// Whether this is an empty view (View(), EmptyView()), e.g. a style's label for a control
    /// made without one.
    ///
    /// @return true for an empty view.
    [[nodiscard]] ELG_API bool is_empty() const noexcept;

    /// Components: any type with `View body() const` can be used as a view. `body()` runs when
    /// the view is mounted (each time it is), not here.
    ///
    /// @param component The component; copied into the view.
    template <class Component>
        requires(!std::derived_from<std::remove_cvref_t<Component>, View> &&
                 requires(const Component& c) {
                     { c.body() } -> std::convertible_to<View>;
                 })
    View(Component component)
        : View(deferred([component = std::move(component)] { return View(component.body()); })) {}

    // --- The one entry point for modifiers; built-in and user modifiers alike. ---

    /// Applies a modifier: a type derived from one of the protocols in elg/modifier.h
    /// (ViewModifier, LayoutModifier, ...).
    ///
    /// @param modifier The modifier; copied into the view.
    /// @return This view, modified by `modifier`.
    template <std::derived_from<Modifier> M>
    [[nodiscard]] View modifier(M modifier) const {
        return with_modifier(std::make_shared<const M>(std::move(modifier)));
    }

    /// Function-style helpers: v.apply(card) == card(v).
    ///
    /// @param function Called with this view.
    /// @return What `function` returns.
    template <std::invocable<View> F>
    [[nodiscard]] View apply(F&& function) const {
        return std::invoke(std::forward<F>(function), *this);
    }

    // --- Layout (elg/modifiers/layout.h) ---

    /// Space around the content, on every edge.
    ///
    /// @param length The space in points. Reactive.
    /// @return This view, inset by `length`.
    [[nodiscard]] ELG_API View padding(Prop<float> length = 16.f) const;
    /// Space around the content, on some edges.
    ///
    /// @param edges The edges to pad, e.g. `Edges::horizontal` or `Edges::top | Edges::leading`.
    /// @param length The space in points. Reactive.
    /// @return This view, inset by `length` on `edges`.
    [[nodiscard]] ELG_API View padding(Edges edges, Prop<float> length = 16.f) const;
    /// Space around the content, per edge. Also takes {.top = 4, ...}.
    ///
    /// @param insets The space on each edge, in points.
    /// @return This view, inset by `insets`.
    [[nodiscard]] ELG_API View padding(EdgeInsets insets) const;
    /// Like padding(EdgeInsets), but following state: laid out again whenever the insets change.
    ///
    /// @param insets The space on each edge, in points. Reactive.
    /// @return This view, inset by `insets`.
    [[nodiscard]] ELG_API View padding(Prop<EdgeInsets> insets) const;

    /// A frame of fixed dimensions around the content: .frame({.width = 100}). Unset dimensions
    /// follow the content. The content is offered the frame's size (the parent's proposal on
    /// unset dimensions) and aligned in it.
    ///
    /// @param options The width, height and alignment.
    /// @return This view in the frame.
    [[nodiscard]] ELG_API View frame(FrameOptions options) const;
    /// Like frame(FrameOptions), with both dimensions fixed.
    ///
    /// @param width The frame's width in points.
    /// @param height The frame's height in points.
    /// @param alignment Where the content sits in the frame when its size differs.
    /// @return This view in a `width` by `height` frame.
    [[nodiscard]] ELG_API View frame(float width, float height, Alignment alignment = Alignment::center) const;
    /// A frame whose size is a range: .frame({.max_width = infinity}). On each axis the frame
    /// takes the proposed length clamped to the bounds; a bound that isn't set comes from the
    /// content, so .frame({.max_width = infinity}) fills the proposal and .frame({.min_width = 100})
    /// is at least 100. The content is offered the clamped proposal (the ideal length when the
    /// parent proposes none) and aligned in the frame.
    ///
    /// @param options The bounds, ideal lengths and alignment.
    /// @return This view in the frame.
    [[nodiscard]] ELG_API View frame(FlexibleFrameOptions options) const;
    /// A fixed size that follows state, like frame(width, height) but laid out again whenever the
    /// size changes (a panel or window resized by dragging). The content is offered that size.
    ///
    /// @code
    /// content.frame([=] { return Size{width(), height()}; })
    /// @endcode
    ///
    /// @param size The frame's size in points. Reactive.
    /// @param alignment Where the content sits in the frame when its size differs.
    /// @return This view in the frame.
    [[nodiscard]] ELG_API View frame(Prop<Size> size, Alignment alignment = Alignment::center) const;

    /// Proposes nothing on the given axes, so the content takes its ideal size.
    ///
    /// @param horizontal Whether the width is the content's ideal width.
    /// @param vertical Whether the height is the content's ideal height.
    /// @return This view, at its ideal size on those axes.
    [[nodiscard]] ELG_API View fixed_size(bool horizontal = true, bool vertical = true) const;
    /// Sizes the view to an aspect ratio (width / height) within what's proposed; without a
    /// ratio, the view's own ideal size gives it (an image's). Content with no ideal size (0 on an
    /// axis) is proposed sizes as they come.
    ///
    /// @param mode Whether the content fits in the proposed size or covers it.
    /// @return This view, proposed sizes at its ideal size's aspect ratio.
    [[nodiscard]] ELG_API View aspect_ratio(ContentMode mode) const;
    /// Sizes the view to an aspect ratio within what's proposed: the content is offered the
    /// largest size at `ratio` that fits the proposal (ContentMode::fit) or the smallest that
    /// covers it (ContentMode::fill). With nothing proposed, its ideal width at `ratio`.
    ///
    /// @param ratio Width divided by height, e.g. 16.f / 9.
    /// @param mode Whether the content fits in the proposed size or covers it.
    /// @return This view, proposed sizes at `ratio`.
    /// @throws UsageError if `ratio` isn't positive and finite.
    [[nodiscard]] ELG_API View aspect_ratio(float ratio, ContentMode mode = ContentMode::fit) const;
    /// Raster Images: take whatever size they're offered instead of their own (stretching to
    /// it; combine with aspect_ratio to keep proportions).
    ///
    /// @return This image, taking the size it's offered.
    /// @throws UsageError on other views, including an Image that already has modifiers (icons
    ///   size with .font()).
    [[nodiscard]] ELG_API View resizable() const;
    /// Stacks give space to higher priorities first; in a ZStack, the highest-priority children
    /// decide its size and the others are offered that size.
    ///
    /// @param priority The priority; views have 0 by default.
    /// @return This view, with layout priority `priority`.
    [[nodiscard]] ELG_API View layout_priority(float priority) const;
    /// Stacking order among siblings (any stack, not just ZStack): higher draws in front and is hit
    /// first. Equal values keep declaration order (later in front).
    ///
    /// @param z The order; views have 0 by default. Reactive.
    /// @return This view, drawn and hit at `z` among its siblings.
    [[nodiscard]] ELG_API View z_index(Prop<float> z) const;
    /// Moves the content when drawn, without affecting layout (and animates the move during
    /// animated updates). It's hit where it's drawn.
    ///
    /// @code
    /// .offset([=] { return open() ? 0.f : -200.f; }, 0)
    /// @endcode
    ///
    /// @param x Points to move right (negative: left). Reactive.
    /// @param y Points to move down (negative: up). Reactive.
    /// @return This view, drawn moved by (`x`, `y`).
    [[nodiscard]] ELG_API View offset(Prop<float> x, Prop<float> y) const;

    // --- Grids (elg/grid.h) ---

    /// A Grid cell that spans `count` columns.
    ///
    /// @param count The number of columns; less than 1 counts as 1.
    /// @return This view, spanning `count` columns of its Grid.
    [[nodiscard]] ELG_API View grid_cell_columns(int count) const;
    /// Where this view sits in its grid cell, instead of its row's, column's, or grid's alignment.
    ///
    /// @param alignment Where it sits in the cell.
    /// @return This view, aligned by `alignment` in its cell.
    [[nodiscard]] ELG_API View grid_cell_alignment(Alignment alignment) const;
    /// The horizontal alignment of every cell in this cell's Grid column.
    ///
    /// @param alignment Where the column's cells sit across its width.
    /// @return This view, setting its column's alignment.
    [[nodiscard]] ELG_API View grid_column_alignment(HorizontalAlignment alignment) const;

    // --- Decoration (elg/modifiers/decoration.h) ---
    // The shorthands are exactly their compositions, drawn and hit tested the same way:
    //   .background(style)        == .background(Rectangle().fill(style))
    //   .background(style, shape) == .background(shape.fill(style))
    //   .border(color, width)     == .overlay(Rectangle().stroke_border(color, width))
    //   .border(color, width, shape) == .overlay(shape.stroke_border(color, width))
    //   .corner_radius(r)         == .clip_shape(RoundedRectangle(r))
    // A shape takes the same points whether it's filled, a clip_shape or a content_shape; a
    // stroke-only shape takes only its line.

    /// Draws `view` behind the content. It's offered exactly the content's size and aligned in it,
    /// and doesn't affect layout. Hits go to the content first.
    ///
    /// @param view The background.
    /// @param alignment Where `view` sits when its size differs from the content's.
    /// @return This view, with `view` behind it.
    [[nodiscard]] ELG_API View background(View view, Alignment alignment = Alignment::center) const;
    /// A color or gradient filling the view's bounds. Reactive arguments work:
    /// .background([=] { return selected() ? accent : surface; })
    ///
    /// @param style The fill. Reactive.
    /// @return This view, with `style` behind it.
    [[nodiscard]] ELG_API View background(Prop<ShapeStyle> style) const;
    /// A color or gradient filling the given shape, which fills the view's bounds.
    ///
    /// @param style The fill. Reactive.
    /// @param shape The shape to fill, e.g. RoundedRectangle(8).
    /// @return This view, with `shape` filled with `style` behind it.
    [[nodiscard]] ELG_API View background(Prop<ShapeStyle> style, const Shape& shape) const;
    /// Draws `view` in front of the content. It's offered exactly the content's size and aligned
    /// in it, and doesn't affect layout. Hits go to `view` first.
    ///
    /// @param view The overlay.
    /// @param alignment Where `view` sits when its size differs from the content's.
    /// @return This view, with `view` in front of it.
    [[nodiscard]] ELG_API View overlay(View view, Alignment alignment = Alignment::center) const;
    /// A border drawn inside the view's bounds.
    ///
    /// @param color The line's color. Reactive.
    /// @param width The line's width in points.
    /// @return This view, with the border in front of it.
    [[nodiscard]] ELG_API View border(Prop<Color> color, float width = 1) const;
    /// A border drawn along the given shape (inside it), which fills the view's bounds.
    ///
    /// @param color The line's color. Reactive.
    /// @param width The line's width in points.
    /// @param shape The shape to outline, e.g. Capsule().
    /// @return This view, with the border in front of it.
    [[nodiscard]] ELG_API View border(Prop<Color> color, float width, const Shape& shape) const;

    // --- Effects (elg/modifiers/effects.h) ---

    /// Clips the content to the view's bounds: clip_shape(Rectangle()).
    ///
    /// @return This view, clipped to its bounds.
    [[nodiscard]] ELG_API View clipped() const;
    /// Clips the content to a shape filling the view's bounds (cheap for rounded rects/circles).
    /// Pointer events outside the shape miss the view too.
    ///
    /// @param shape The shape to clip to.
    /// @return This view, clipped to `shape`.
    [[nodiscard]] ELG_API View clip_shape(const Shape& shape) const;
    /// Shorthand for clip_shape(RoundedRectangle(radius)).
    ///
    /// @param radius The corner radius in points.
    /// @return This view, clipped to its bounds with rounded corners.
    [[nodiscard]] ELG_API View corner_radius(float radius) const;
    /// Fades the view as a group: overlapping content doesn't show through itself. A view at
    /// opacity 0 still takes the pointer (allows_hit_testing(false) lets it through).
    ///
    /// @param opacity 0 (invisible) to 1 (unchanged); values outside are clamped. Reactive and
    ///   animatable.
    /// @return This view, drawn at `opacity`.
    [[nodiscard]] ELG_API View opacity(Prop<float> opacity) const;
    /// A blurred shadow of the view's content.
    ///
    /// @param shadow The shadow's color, blur radius, offset and spread.
    /// @return This view, casting `shadow`.
    [[nodiscard]] ELG_API View shadow(Shadow shadow = {}) const;
    /// A shadow cast inside the view's content, from its edges inward (pressed-in, embossed).
    ///
    /// @param shadow The shadow's color, blur radius, offset and spread. The default is 25% black,
    ///   radius 6, 2 points down.
    /// @return This view, with `shadow` inside it.
    [[nodiscard]] ELG_API View inner_shadow(Shadow shadow = {.color = Color::black.opacity(0.25f), .radius = 6, .y = 2}) const;
    /// Gaussian blur of the content.
    ///
    /// @param radius The blur radius in points. Reactive and animatable.
    /// @param opaque Whether the content fills its bounds: its edges then stay sharp instead of
    ///   fading out.
    /// @return This view, blurred.
    [[nodiscard]] ELG_API View blur(Prop<float> radius, bool opaque = false) const;
    /// Blurs what's drawn behind the view in the window (frosted glass), inside the view's bounds
    /// (or a shape: the overload below). Like .background, it goes behind everything it wraps, so
    /// a tint goes on before it:
    ///
    /// @code
    /// Text("Now playing").padding(12)
    ///     .background(Color::white.opacity(0.5f), RoundedRectangle(12))
    ///     .backdrop_blur(30, RoundedRectangle(12), {.saturation = 1.8f, .noise = 0.02f})
    /// @endcode
    ///
    /// .blur() after it blurs the view, backdrop included. Inside an offscreen group (opacity below
    /// 1, a shadow, a blur, ...) it sees only what the group drew behind it, as CSS's
    /// backdrop-filter does.
    ///
    /// @param radius The blur radius in points. Reactive and animatable.
    /// @param options Saturation, brightness and noise applied after the blur.
    /// @return This view, over the blurred backdrop.
    [[nodiscard]] ELG_API View backdrop_blur(const Prop<float>& radius, const BackdropOptions& options = {}) const;
    /// Like backdrop_blur(radius, options), but inside `shape` (filling the view's bounds).
    ///
    /// @param radius The blur radius in points. Reactive and animatable.
    /// @param shape Where the backdrop shows, e.g. RoundedRectangle(12).
    /// @param options Saturation, brightness and noise applied after the blur.
    /// @return This view, over the blurred backdrop.
    [[nodiscard]] ELG_API View backdrop_blur(const Prop<float>& radius, const Shape& shape, const BackdropOptions& options = {}) const;
    // Color adjustments (SwiftUI's, and CSS's filter functions). Each is reactive and animatable.
    /// Brightens or darkens the content: adds `amount` to each channel.
    ///
    /// @param amount -1 black ... 0 unchanged ... 1 white.
    /// @return This view, brightness-adjusted.
    [[nodiscard]] ELG_API View brightness(Prop<float> amount) const;
    /// Raises or lowers the content's contrast.
    ///
    /// @param amount 0 flat gray, 1 unchanged, above 1 more contrast.
    /// @return This view, contrast-adjusted.
    [[nodiscard]] ELG_API View contrast(Prop<float> amount) const;
    /// Makes the content's colors more or less vivid.
    ///
    /// @param amount 0 gray, 1 unchanged, above 1 more vivid.
    /// @return This view, saturation-adjusted.
    [[nodiscard]] ELG_API View saturation(Prop<float> amount) const;
    /// Turns the content's colors gray.
    ///
    /// @param amount 0 unchanged ... 1 fully gray.
    /// @return This view, grayed.
    [[nodiscard]] ELG_API View grayscale(Prop<float> amount) const;
    /// Rotates hues around the color wheel by `degrees`.
    ///
    /// @param degrees How far to turn each hue.
    /// @return This view, hue-rotated.
    [[nodiscard]] ELG_API View hue_rotation(Prop<float> degrees) const;
    /// Tones the content's colors sepia.
    ///
    /// @param amount 0 unchanged ... 1 fully sepia-toned.
    /// @return This view, sepia-toned.
    [[nodiscard]] ELG_API View sepia(Prop<float> amount) const;
    /// Inverts the colors, or partly (0...1).
    ///
    /// @param amount 0 unchanged ... 1 fully inverted.
    /// @return This view, inverted.
    [[nodiscard]] ELG_API View color_invert(Prop<float> amount = 1.f) const;
    /// Multiplies every pixel by `color` (alpha too): tints white content, darkens the rest.
    ///
    /// @param color The color to multiply by.
    /// @return This view, multiplied by `color`.
    [[nodiscard]] ELG_API View color_multiply(Prop<Color> color) const;
    /// Brightness becomes opacity: white stays, black disappears (for masks).
    ///
    /// @return This view, its luminance as its alpha.
    [[nodiscard]] ELG_API View luminance_to_alpha() const;
    /// Blends the view with what's behind it.
    ///
    /// @param mode How its pixels combine with what's behind them.
    /// @return This view, blended with `mode`.
    [[nodiscard]] ELG_API View blend_mode(BlendMode mode) const;
    /// Draws the content offscreen first, then composites it as one image: blend modes inside it
    /// blend with each other only, and a material inside sees only what the group drew.
    ///
    /// @return This view, drawn as one offscreen group.
    [[nodiscard]] ELG_API View compositing_group() const;
    /// Rotates the drawn content by `degrees` (clockwise) about `anchor`, without affecting layout
    /// or hit testing.
    ///
    /// @param degrees The angle, clockwise. Reactive and animatable.
    /// @param anchor The point it turns about, in unit coordinates of the view's bounds.
    /// @return This view, drawn rotated.
    [[nodiscard]] ELG_API View rotation_effect(Prop<float> degrees, UnitPoint anchor = UnitPoint::center) const;
    /// Scales the drawn content about `anchor`, without affecting layout or hit testing. A uniform
    /// scale stays sharp (vector); an uneven one renders offscreen first.
    ///
    /// @param scale The factor on both axes (1 unchanged; 0 draws nothing). Reactive and animatable.
    /// @param anchor The point that stays put, in unit coordinates of the view's bounds.
    /// @return This view, drawn scaled.
    [[nodiscard]] ELG_API View scale_effect(const Prop<float>& scale, UnitPoint anchor = UnitPoint::center) const;
    /// Like scale_effect(scale, anchor), with a factor per axis.
    ///
    /// @param x The horizontal factor. Reactive and animatable.
    /// @param y The vertical factor. Reactive and animatable.
    /// @param anchor The point that stays put, in unit coordinates of the view's bounds.
    /// @return This view, drawn scaled.
    [[nodiscard]] ELG_API View scale_effect(Prop<float> x, Prop<float> y, UnitPoint anchor = UnitPoint::center) const;
    /// Shows the content only where `mask` (sized and aligned like an overlay) is opaque; its
    /// colors don't matter, only its alpha. Pointer hits reach the content only where the mask
    /// would be hit (on a shape's fill, a text's frame, ...); the mask takes none itself. A
    /// gradient fades the content out:
    ///
    /// @code
    /// list.mask(LinearGradient({Color::black, Color::clear}, UnitPoint::center, UnitPoint::bottom))
    /// @endcode
    ///
    /// @param mask The view whose alpha keeps the content.
    /// @param alignment Where `mask` sits when its size differs from the content's.
    /// @return This view, masked.
    [[nodiscard]] ELG_API View mask(View mask, Alignment alignment = Alignment::center) const;
    /// Like mask(View), with a color or gradient filling the view's bounds as the mask:
    /// mask(Rectangle().fill(style)).
    ///
    /// @param style The fill whose alpha keeps the content. Reactive.
    /// @return This view, masked.
    [[nodiscard]] ELG_API View mask(Prop<ShapeStyle> style) const;
    /// Hit tests against this shape (filling the bounds) instead of the drawn content, e.g.
    /// content_shape(Rectangle()) makes the gaps in a stack tappable. A Button takes its whole
    /// frame; .content_shape(Capsule()) narrows it to its shape.
    ///
    /// @param shape The area that takes the pointer.
    /// @return This view, hit inside `shape`.
    [[nodiscard]] ELG_API View content_shape(const Shape& shape) const;
    /// false: the view and everything in it ignore the pointer; events go to what's behind.
    ///
    /// @param enabled Whether the view can be hit.
    /// @return This view, ignoring the pointer unless `enabled`.
    [[nodiscard]] ELG_API View allows_hit_testing(bool enabled) const;

    // --- Events (elg/modifiers/events.h) ---

    /// Pointer clicks: pressed and released inside the view with the options' button (left by
    /// default) and click count. The innermost click handler wins. Clicks only: for something a
    /// keyboard or a screen reader should be able to use too, use on_activate. A disabled view
    /// (.disabled()) doesn't fire it.
    ///
    /// @code
    /// Canvas(draw).on_click([=](const ClickEvent& e) { add_point(e.location); })
    /// Row().on_click(select).on_double_click(open)
    /// @endcode
    ///
    /// @param action Called with the click, located in the view's own coordinates.
    /// @param options The button and click count.
    /// @return This view, clickable.
    [[nodiscard]] ELG_API View on_click(std::function<void(const ClickEvent&)> action, ClickOptions options = {}) const;
    /// Like on_click(action, options), for an action that doesn't need the event.
    ///
    /// @param action Called on each click.
    /// @param options The button and click count.
    /// @return This view, clickable.
    [[nodiscard]] ELG_API View on_click(std::function<void()> action, ClickOptions options = {}) const;
    /// on_click with .count = 2. The first click goes on to outer click handlers.
    ///
    /// @param action Called with the double click, located in the view's own coordinates.
    /// @return This view, double-clickable.
    [[nodiscard]] ELG_API View on_double_click(std::function<void(const ClickEvent&)> action) const;
    /// on_click with .count = 2, for an action that doesn't need the event.
    ///
    /// @param action Called on each double click.
    /// @return This view, double-clickable.
    [[nodiscard]] ELG_API View on_double_click(std::function<void()> action) const;
    /// The view's action, however it's used: a left click, Space or Enter while it has focus, or a
    /// screen reader's default action. Makes it focusable (Tab, with a focus ring); its
    /// accessibility role stays its own. Disabled views (.disabled) don't activate.
    ///
    /// @code
    /// Tile(album).on_activate([=] { open(album); }).accessibility_label(album.title)
    /// @endcode
    ///
    /// @param action Runs on activation.
    /// @return This view, activatable.
    /// @throws UsageError if `action` is empty.
    [[nodiscard]] ELG_API View on_activate(std::function<void()> action) const;
    /// Gestures (elg/gestures.h): drags and long presses take a press over once they recognize it
    /// (a button or on_click under them then doesn't fire); pinches, rotations and two-finger double
    /// taps come from trackpads.
    ///
    /// @code
    /// .recognize(DragGesture().on_began([=] { start = offset.peek(); })
    ///                         .on_changed([=](const DragEvent& e) { offset = start() + e.translation; }))
    /// @endcode
    ///
    /// GestureOptions: {.priority = GesturePriority::high} goes before gestures inside the view;
    /// {.simultaneous = true} runs alongside whatever has the press.
    ///
    /// @code
    /// row.recognize(DragGesture().on_changed(swipe), {.priority = GesturePriority::high})
    /// @endcode
    ///
    /// @param gesture The drag and its handlers.
    /// @param options Its priority over gestures inside the view, or whether it runs alongside.
    /// @return This view, recognizing `gesture`.
    [[nodiscard]] ELG_API View recognize(DragGesture gesture, GestureOptions options = {}) const;
    /// Like recognize(DragGesture, GestureOptions), for a long press.
    ///
    /// @param gesture The long press and its handler.
    /// @param options Its priority over gestures inside the view, or whether it runs alongside.
    /// @return This view, recognizing `gesture`.
    [[nodiscard]] ELG_API View recognize(LongPressGesture gesture, GestureOptions options = {}) const;
    /// A trackpad pinch over the view.
    ///
    /// @param gesture The pinch and its handlers.
    /// @return This view, recognizing `gesture`.
    [[nodiscard]] ELG_API View recognize(MagnifyGesture gesture) const;
    /// A trackpad two-finger rotation over the view.
    ///
    /// @param gesture The rotation and its handlers.
    /// @return This view, recognizing `gesture`.
    [[nodiscard]] ELG_API View recognize(RotateGesture gesture) const;
    /// A trackpad two-finger double tap over the view.
    ///
    /// @param gesture The double tap and its handler.
    /// @return This view, recognizing `gesture`.
    [[nodiscard]] ELG_API View recognize(TwoFingerDoubleTapGesture gesture) const;
    /// Shorthand for recognize(DragGesture().on_changed(action)).
    ///
    /// @param action Called as the drag moves.
    /// @return This view, draggable.
    [[nodiscard]] ELG_API View on_drag(std::function<void(const DragEvent&)> action) const;
    /// Shorthand for recognize(LongPressGesture(action)).
    ///
    /// @param action Called when the long press is recognized.
    /// @return This view, recognizing long presses.
    [[nodiscard]] ELG_API View on_long_press(std::function<void()> action) const;
    /// Shorthand for recognize(MagnifyGesture().on_changed(action)).
    ///
    /// @param action Called as the pinch changes.
    /// @return This view, recognizing pinches.
    [[nodiscard]] ELG_API View on_magnify(std::function<void(const MagnifyEvent&)> action) const;
    /// Shorthand for recognize(RotateGesture().on_changed(action)).
    ///
    /// @param action Called as the rotation changes.
    /// @return This view, recognizing rotations.
    [[nodiscard]] ELG_API View on_rotate(std::function<void(const RotateEvent&)> action) const;
    /// Shorthand for recognize(TwoFingerDoubleTapGesture(action)).
    ///
    /// @param action Called with where the tap was.
    /// @return This view, recognizing two-finger double taps.
    [[nodiscard]] ELG_API View on_two_finger_double_tap(std::function<void(Point)> action) const;
    /// The pointer's shape over the view, like CSS's `cursor`: .pointer_style(PointerStyle::pointer)
    /// over something to click. Views inside can set their own (a text field's I-beam).
    ///
    /// @code
    /// .pointer_style([=] { return busy() ? PointerStyle::wait : PointerStyle::automatic; })
    /// @endcode
    ///
    /// @param style The pointer's shape. Reactive.
    /// @return This view, showing `style` under the pointer.
    [[nodiscard]] ELG_API View pointer_style(Prop<PointerStyle> style) const;
    /// Called at once when the pointer comes over the view (its hit area): for highlights.
    ///
    /// @code
    /// .on_pointer_enter([=] { hovered = true; }).on_pointer_leave([=] { hovered = false; })
    /// @endcode
    ///
    /// @param action Called when the pointer enters.
    /// @return This view, watching the pointer come in.
    [[nodiscard]] ELG_API View on_pointer_enter(std::function<void()> action) const;
    /// Called at once when the pointer leaves the view (its hit area): for highlights.
    ///
    /// @param action Called when the pointer leaves.
    /// @return This view, watching the pointer go.
    [[nodiscard]] ELG_API View on_pointer_leave(std::function<void()> action) const;
    /// Raw pointer events over the view (down, up, moves while captured, hover moves, cancel),
    /// located in its own coordinates. Return true to consume one; consuming a down captures the
    /// pointer, so the moves and the up come here wherever it goes. For drawing and custom
    /// controls; prefer on_click and gestures otherwise.
    ///
    /// @param action Called with each event; returns whether it consumed it.
    /// @return This view, handling pointer events.
    [[nodiscard]] ELG_API View on_pointer(std::function<bool(const PointerEvent&)> action) const;
    /// Wheel and trackpad scrolling over the view, located in its own coordinates. Return true to
    /// consume it (scroll views around it then don't scroll).
    ///
    /// @param action Called with each scroll; returns whether it consumed it.
    /// @return This view, handling scrolling.
    [[nodiscard]] ELG_API View on_scroll(std::function<bool(const ScrollEvent&)> action) const;
    /// Hover intent: called with true once the pointer has rested on the view for the hover
    /// delay's `enter` (passing over faster doesn't count), and false when it leaves (after its
    /// `exit`): for previews and other things that shouldn't flash as the pointer passes. The
    /// delay comes from the environment (no wait by default): .on_hover_change(action).hover_delay({.enter = 0.5})
    ///
    /// @param action Called with whether the view is hovered.
    /// @return This view, reporting hover changes.
    [[nodiscard]] ELG_API View on_hover_change(std::function<void(bool)> action) const;
    /// Called after initial layout and when physical dimensions change, including display scale.
    /// Dimensions are ceil(logical size * display scale), before visual transforms, and may be zero.
    /// Runs on the UI thread before paint: queue renderer input, do not mutate layout here.
    ///
    /// @param action Called with the view's size in physical pixels.
    /// @return This view, reporting its size.
    [[nodiscard]] ELG_API View on_size_change(std::function<void(PixelSize)> action) const;
    /// Files or text dragged from other apps (Explorer, a browser) can be dropped here. The
    /// innermost view under the pointer that takes that kind of thing gets it. A disabled view
    /// takes nothing.
    ///
    /// @param options The handlers for files and text, and for a drag over the view.
    /// @return This view, a drop target.
    /// @throws UsageError if `options` has neither a `files` nor a `text` handler.
    [[nodiscard]] ELG_API View on_drop(DropOptions options) const;

    // --- Lifecycle ---
    /// Runs `action` once the view is added: its window opened, an If/ForEach showed it, or a lazy
    /// stack built it as it scrolled near. It runs after the update that added it, before that
    /// update is drawn, as the view's own: timers (elg/timer.h) and effects started in it end
    /// with the view.
    ///
    /// @code
    /// Spinner().on_appear([=] { spinning = true; })
    /// @endcode
    ///
    /// @param action Runs when the view appears.
    /// @return This view, running `action` when it appears.
    [[nodiscard]] ELG_API View on_appear(std::function<void()> action) const;
    /// Runs `action` once the view is gone: after its exit transition, or when its window closed.
    /// Only for views that appeared.
    ///
    /// @param action Runs when the view disappears.
    /// @return This view, running `action` when it disappears.
    [[nodiscard]] ELG_API View on_disappear(std::function<void()> action) const;

    // --- Accessibility (design.md §12) ---
    // Built-in views describe themselves to screen readers; these adjust that.
    /// What it's called (an icon-only button needs one: Image("trash") says nothing).
    ///
    /// @param label The name screen readers say. Reactive.
    /// @return This view, labeled.
    [[nodiscard]] ELG_API View accessibility_label(Prop<std::string> label) const;
    /// What it holds, as words ("3 of 10", "50%").
    ///
    /// @param value The value screen readers say. Reactive.
    /// @return This view, with an accessibility value.
    [[nodiscard]] ELG_API View accessibility_value(Prop<std::string> value) const;
    /// What happens if it's used, when the label doesn't say ("Deletes the draft").
    ///
    /// @param hint The hint screen readers give. Reactive.
    /// @return This view, with an accessibility hint.
    [[nodiscard]] ELG_API View accessibility_hint(Prop<std::string> hint) const;
    /// Leaves it (and everything inside) out: decoration.
    ///
    /// @param hidden Whether screen readers skip it.
    /// @return This view, hidden from screen readers if `hidden`.
    [[nodiscard]] ELG_API View accessibility_hidden(bool hidden = true) const;
    /// What the view is to screen readers, instead of the automatic choice.
    ///
    /// @param role Its role.
    /// @return This view, with role `role`.
    [[nodiscard]] ELG_API View accessibility_role(AccessibilityRole role) const;
    /// One element for everything inside, labeled with their text (a row read as one line).
    ///
    /// @return This view, read as one element.
    [[nodiscard]] ELG_API View accessibility_element() const;
    /// What a screen reader's default action (click) does, for views made clickable with gestures.
    ///
    /// @param action Runs on the screen reader's default action.
    /// @return This view, with that action.
    /// @throws UsageError if `action` is empty.
    [[nodiscard]] ELG_API View accessibility_action(std::function<void()> action) const;
    /// The hover delay for on_hover_change handlers and submenus inside (default: enter 0, exit
    /// 0). Controls' highlights and on_pointer_enter/leave don't wait.
    ///
    /// @param delay How long the pointer rests before hover starts, and how long after it leaves
    ///   before hover ends, in seconds.
    /// @return This view, with `delay` for everything inside.
    [[nodiscard]] ELG_API View hover_delay(HoverDelay delay) const;

    /// Pressing and dragging here moves the window (for custom title bars, TitleBar::hidden).
    /// Double-clicking maximizes, like a system title bar. Controls inside still work.
    ///
    /// @param enabled Whether the view drags the window.
    /// @return This view, a window drag area if `enabled`.
    [[nodiscard]] ELG_API View window_drag_area(bool enabled = true) const;

    // --- Popups (design.md §13.1) ---

    /// Right-click (or Shift+F10 / the Menu key while focused) opens the menu `menu` builds,
    /// usually a Menu(...). The builder runs each time it opens; item actions run after it closes.
    /// It doesn't open while the view is disabled.
    ///
    /// @code
    /// Text(file.name).context_menu([=] { return Menu(MenuItem("Rename", rename), Separator(),
    ///                                                MenuItem("Delete", remove, {.destructive = true})); })
    /// @endcode
    ///
    /// @param menu Builds the menu.
    /// @return This view, with the context menu.
    [[nodiscard]] ELG_API View context_menu(std::function<View()> menu) const;
    /// A tooltip: `content` (in the environment's TooltipStyle) shown after the pointer rests on
    /// the view for a moment, until it leaves or presses. Screen readers don't see it.
    ///
    /// @code
    /// Avatar(user).tooltip([=] { return HStack(Image("user"), Text(user.name)); })
    /// @endcode
    ///
    /// @param content Builds the tooltip's content.
    /// @param options Where it goes, how long the pointer rests first, and how it moves.
    /// @return This view, with the tooltip.
    /// @throws UsageError if `content` is empty.
    [[nodiscard]] ELG_API View tooltip(std::function<View()> content, TooltipOptions options = {}) const;
    /// A text tooltip: tooltip([=] { return Text(text); }, options).
    ///
    /// @code
    /// Button(Image("gear"), settings).tooltip("Settings")
    /// @endcode
    ///
    /// @param text The tooltip's text. Reactive.
    /// @param options Where it goes, how long the pointer rests first, and how it moves.
    /// @return This view, with the tooltip.
    [[nodiscard]] ELG_API View tooltip(Prop<std::string> text, TooltipOptions options = {}) const;
    /// `content` floating above everything by the view, while `is_presented` is true, with no
    /// chrome of its own. The builder runs each time it opens. What closes it (and sets the
    /// binding back to false), where it goes and whether it may leave the window are `options`'.
    ///
    /// @code
    /// Avatar().floating(card_shown.binding(), [=] { return ProfileCard(user); },
    ///                   {.placement = {.edge = FloatingEdge::trailing, .spacing = 8},
    ///                    .boundary = FloatingBoundary::window})
    /// @endcode
    ///
    /// @param is_presented Whether it shows; set back to false when it's dismissed.
    /// @param content Builds what floats.
    /// @param options Where it goes, how far it may go, what closes it, and how it moves.
    /// @return This view, with the floating content.
    /// @throws UsageError if `content` is empty.
    [[nodiscard]] ELG_API View floating(Binding<bool> is_presented, std::function<View()> content,
                          FloatingOptions options = {}) const;
    /// Marks the view as where FloatingBoundary::container content inside it stays: a map whose
    /// callouts mustn't cover the toolbar beside it, a card in a scroll view.
    ///
    /// @code
    /// MapView(pins).floating_container()
    /// @endcode
    ///
    /// @return This view, a floating container.
    [[nodiscard]] ELG_API View floating_container() const;
    /// A panel of `content` beside the view, shown while `is_presented` is true. Clicking outside
    /// it, Esc, or switching apps closes it and sets `is_presented` back to false. The builder runs
    /// each time it opens; the content can be anything, e.g. a small form. The panel comes from
    /// the environment's PopoverStyle.
    ///
    /// @code
    /// Button("Details", [=] { showing = true; })
    ///     .popover(showing.binding(), [=] { return VStack(Text("Release notes"), ...); })
    /// @endcode
    ///
    /// @param is_presented Whether it shows; set back to false when it's dismissed.
    /// @param content Builds the panel's content.
    /// @param options Where it goes, how far it may go, padding, and how it moves.
    /// @return This view, with the popover.
    [[nodiscard]] ELG_API View popover(Binding<bool> is_presented, std::function<View()> content,
                         const PopoverOptions& options = {}) const;

    // --- Keyboard and focus (design.md §10) ---

    /// true: the view takes keyboard focus (Tab order, clicks); controls are focusable already.
    /// false: the view and everything inside it never take focus (Tab skips them, clicking them
    /// leaves focus where it was), e.g. toolbar or caption buttons.
    ///
    /// @param focusable Whether the view takes focus, or nothing inside it does.
    /// @return This view, focusable or not.
    [[nodiscard]] ELG_API View focusable(bool focusable = true) const;
    /// Two-way focus: the binding is true while the view (or something inside) has focus, and
    /// setting it true focuses the first focusable view in it (false removes focus). Makes the
    /// view focusable when nothing inside is.
    ///
    /// @param is_focused Whether the view has focus.
    /// @return This view, its focus bound to `is_focused`.
    [[nodiscard]] ELG_API View focused(Binding<bool> is_focused) const;
    /// Runs `action` when `key` is pressed while the view (or something inside) has focus,
    /// whatever modifier keys are held, and again for key repeats. The key is then handled.
    ///
    /// @param key The key.
    /// @param action Runs on each press.
    /// @return This view, handling `key`.
    [[nodiscard]] ELG_API View on_key_press(Key key, std::function<void()> action) const;
    /// Sees every key down and up while the view (or something inside) has focus; return true if
    /// handled (unhandled keys bubble out).
    ///
    /// @param action Called with each key event; returns whether it handled it.
    /// @return This view, handling keys.
    [[nodiscard]] ELG_API View on_key(std::function<bool(const KeyEvent&)> action) const;
    /// Makes the view focusable and sees the text typed while it has focus: commits, and an IME's
    /// compositions in progress. Keys still go to on_key. Return true if handled.
    ///
    /// @param action Called with each text input; returns whether it handled it.
    /// @return This view, taking text input.
    [[nodiscard]] ELG_API View on_text_input(std::function<bool(const TextInputEvent&)> action) const;

    // --- Selection ---

    /// Makes the rows of a stack over a ForEach selectable, by their ForEach keys (the key type
    /// here is the ForEach's). Put it on the VStack, HStack, LazyVStack or LazyHStack itself; its
    /// other children (a header) aren't rows. Each row is drawn by the environment's RowStyle
    /// (row_style()). Without such a stack inside, it logs a warning.
    ///
    /// @code
    /// const State<std::optional<int>> open;
    /// ScrollView(LazyVStack(ForEach(mails, &Mail::id, MailRow))
    ///                .selection(open.binding(), {.on_activate = [=](int id) { show(id); }}))
    /// @endcode
    ///
    /// - A click selects a row. Clicks a control in the row takes (a Button) don't.
    /// - The stack is one Tab stop. Up/Down (Left/Right in an HStack) move the selection, Home/End
    ///   go to the ends, and Enter calls on_activate, as does a double click. The selected row
    ///   scrolls into view, in a lazy stack too.
    /// - Screen readers see a list box whose rows are its options.
    ///
    /// @param selection The selected row's key, or nullopt for none.
    /// @param options What Enter and double clicks do.
    /// @return This stack, its rows selectable.
    template <class K>
    [[nodiscard]] View selection(const Binding<std::optional<K>>& selection, SelectionOptions<K> options = {}) const {
        detail::SelectionModel model;
        model.get = [selection] {
            const std::optional<K> key = selection.get();
            return key ? std::vector<ItemKey>{detail::to_item_key(*key)} : std::vector<ItemKey>{};
        };
        model.contains = [selection](const ItemKey& key) {
            const std::optional<K> selected = selection.get();
            return selected && detail::to_item_key(*selected) == key;
        };
        model.set = [selection](std::vector<ItemKey> keys) {
            selection.set(keys.empty() ? std::nullopt : std::optional<K>(detail::from_item_key<K>(keys.front())));
        };
        if (options.on_activate) {
            model.activate = [activate = std::move(options.on_activate)](const ItemKey& key) {
                activate(detail::from_item_key<K>(key));
            };
        }
        return selection_impl(std::move(model));
    }
    /// Like selection(const Binding<std::optional<K>>&, SelectionOptions<K>), but with a std::set,
    /// more than one: Ctrl+click (Cmd on macOS) adds or removes a row, Shift+click and
    /// Shift+arrows select the range from where it started, and Ctrl+A (Cmd+A) selects all.
    ///
    /// @param selection The selected rows' keys.
    /// @param options What Enter and double clicks do.
    /// @return This stack, its rows selectable.
    template <class K>
    [[nodiscard]] View selection(const Binding<std::set<K>>& selection, SelectionOptions<K> options = {}) const {
        detail::SelectionModel model;
        model.multiple = true;
        model.get = [selection] {
            std::vector<ItemKey> keys;
            for (const K& key : selection.get()) {
                keys.push_back(detail::to_item_key(key));
            }
            return keys;
        };
        model.contains = [selection](const ItemKey& key) {
            return selection.get().contains(detail::from_item_key<K>(key));
        };
        model.set = [selection](const std::vector<ItemKey>& keys) {
            std::set<K> selected;
            for (const ItemKey& key : keys) {
                selected.insert(detail::from_item_key<K>(key));
            }
            selection.set(std::move(selected));
        };
        if (options.on_activate) {
            model.activate = [activate = std::move(options.on_activate)](const ItemKey& key) {
                activate(detail::from_item_key<K>(key));
            };
        }
        return selection_impl(std::move(model));
    }

    // --- Environment (elg/modifiers/environment.h): inherited by everything inside. ---

    /// The weight of the font inside, keeping its size and family (text, and icons of sets with
    /// weights): Image("gear").font_weight(Weight::bold).
    ///
    /// @param weight The weight. Reactive.
    /// @return This view, its font at `weight`.
    [[nodiscard]] ELG_API View font_weight(Prop<Weight> weight) const;
    /// The font of text inside (and the size of icons).
    ///
    /// @param font The font. Reactive.
    /// @return This view, with `font` inside.
    [[nodiscard]] ELG_API View font(Prop<Font> font) const;
    /// The color of text and unfilled shapes. Also takes a ThemeColor.
    ///
    /// @param color The color. Reactive.
    /// @return This view, with `color` inside.
    [[nodiscard]] ELG_API View foreground_style(Prop<Color> color) const;
    /// The accent color of controls (e.g. Button).
    ///
    /// @param color The accent color. Reactive.
    /// @return This view, with `color` as the tint inside.
    [[nodiscard]] ELG_API View tint(Prop<Color> color) const;
    /// Light or dark, for ThemeColor tokens inside. Windows follow the system setting by default.
    ///
    /// @param scheme Light or dark. Reactive.
    /// @return This view, in `scheme`.
    [[nodiscard]] ELG_API View color_scheme(Prop<ColorScheme> scheme) const;
    /// The theme ThemeColor and ThemeBlur tokens inside resolve against.
    ///
    /// @param theme The color and blur tokens.
    /// @return This view, in `theme`.
    [[nodiscard]] ELG_API View theme(Theme theme) const;
    /// Controls inside stop responding and look disabled.
    ///
    /// @param disabled Whether they're disabled. Reactive.
    /// @return This view, disabled if `disabled`.
    [[nodiscard]] ELG_API View disabled(Prop<bool> disabled = true) const;
    /// The style of every Button inside (elg/styles.h).
    ///
    /// @param style The style; copied.
    /// @return This view, with `style` for its buttons.
    template <class Style>
        requires std::derived_from<Style, ButtonStyle>
    [[nodiscard]] View button_style(Style style) const {
        return button_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    /// The style of the selectable rows inside (selection(), elg/styles.h).
    ///
    /// @param style The style; copied.
    /// @return This view, with `style` for its selectable rows.
    template <class Style>
        requires std::derived_from<Style, RowStyle>
    [[nodiscard]] View row_style(Style style) const {
        return row_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    /// The style of every Picker inside (elg/styles.h).
    ///
    /// @param style The style; copied.
    /// @return This view, with `style` for its pickers.
    template <class Style>
        requires std::derived_from<Style, PickerStyle>
    [[nodiscard]] View picker_style(Style style) const {
        return picker_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    /// The style of every Slider inside (elg/styles.h).
    ///
    /// @param style The style; copied.
    /// @return This view, with `style` for its sliders.
    template <class Style>
        requires std::derived_from<Style, SliderStyle>
    [[nodiscard]] View slider_style(Style style) const {
        return slider_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    /// The style of every Toggle inside (elg/styles.h).
    ///
    /// @param style The style; copied.
    /// @return This view, with `style` for its toggles.
    template <class Style>
        requires std::derived_from<Style, ToggleStyle>
    [[nodiscard]] View toggle_style(Style style) const {
        return toggle_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    /// The style of every Checkbox inside (elg/styles.h).
    ///
    /// @param style The style; copied.
    /// @return This view, with `style` for its checkboxes.
    template <class Style>
        requires std::derived_from<Style, CheckboxStyle>
    [[nodiscard]] View checkbox_style(Style style) const {
        return checkbox_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    /// The style of every NumberInput inside (elg/styles.h).
    ///
    /// @param style The style; copied.
    /// @return This view, with `style` for its number inputs.
    template <class Style>
        requires std::derived_from<Style, NumberInputStyle>
    [[nodiscard]] View number_input_style(Style style) const {
        return number_input_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    /// The style of popovers opened from inside (elg/styles.h).
    ///
    /// @param style The style; copied.
    /// @return This view, with `style` for its popovers.
    template <class Style>
        requires std::derived_from<Style, PopoverStyle>
    [[nodiscard]] View popover_style(Style style) const {
        return popover_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    /// The style of tooltips opened from inside (elg/styles.h).
    ///
    /// @param style The style; copied.
    /// @return This view, with `style` for its tooltips.
    template <class Style>
        requires std::derived_from<Style, TooltipStyle>
    [[nodiscard]] View tooltip_style(Style style) const {
        return tooltip_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    /// The style of menus opened from inside (elg/styles.h).
    ///
    /// @param style The style; copied.
    /// @return This view, with `style` for its menus.
    template <class Style>
        requires std::derived_from<Style, MenuStyle>
    [[nodiscard]] View menu_style(Style style) const {
        return menu_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    /// The style of every Progress inside (elg/styles.h).
    ///
    /// @param style The style; copied.
    /// @return This view, with `style` for its progress views.
    template <class Style>
        requires std::derived_from<Style, ProgressStyle>
    [[nodiscard]] View progress_style(Style style) const {
        return progress_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    /// The style of every NavigationStack inside (elg/navigation.h).
    ///
    /// @param style The style; copied.
    /// @return This view, with `style` for its navigation stacks.
    template <class Style>
        requires std::derived_from<Style, NavigationStackStyle>
    [[nodiscard]] View navigation_stack_style(Style style) const {
        return navigation_stack_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    /// The style of every TextField inside (elg/styles.h).
    ///
    /// @param style The style; copied.
    /// @return This view, with `style` for its text fields.
    template <class Style>
        requires std::derived_from<Style, TextFieldStyle>
    [[nodiscard]] View text_field_style(Style style) const {
        return text_field_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    // --- Animation (elg/animation.h) ---

    /// Changes inside this view animate with `animation` whenever `value` (a plain value, State,
    /// or closure; it should be equality-comparable) changed in the same update.
    ///
    /// @param animation The timing of the changes.
    /// @param value What to watch. A closure is read as a tracked read.
    /// @return This view, animating its changes when `value` changes.
    template <class Value>
    [[nodiscard]] View animation(const Animation& animation, Value value) const {
        return animation_impl(animation, detail::change_watcher(std::move(value)));
    }
    /// How this view enters and leaves when an If/ForEach adds or removes it with an animation.
    ///
    /// @param transition How it enters and leaves.
    /// @return This view, entering and leaving with `transition`.
    [[nodiscard]] ELG_API View transition(const Transition& transition) const;
    /// false: this view and everything inside snap to new positions and sizes instead of easing
    /// there. Colors, opacity, and transitions still animate.
    ///
    /// @param enabled Whether layout changes animate.
    /// @return This view, its layout changes animating only if `enabled`.
    [[nodiscard]] ELG_API View animates_layout(bool enabled) const;

    /// Sets any environment key (elg/environment.h): .environment<CompactKey>(true)
    ///
    /// @tparam Key The key: a type with a `Value` type, a `name` and a `default_value()`.
    /// @param value The value, or anything a Prop<Key::Value> takes (reactive).
    /// @return This view, with `value` for `Key` inside.
    template <class Key, class Value>
    [[nodiscard]] View environment(Value&& value) const {
        return modifier(EnvironmentKeyModifier<Key>(std::forward<Value>(value)));
    }

    /// A value for the parent layout to read with LayoutChild::value<Key>() (custom layouts,
    /// elg/layout.h). A key is a type with a `Value` type and a `default_value()`:
    ///
    /// @code
    /// struct Rank { using Value = int; static int default_value() { return 0; } };
    /// Text("Gold").layout_value<Rank>(1)
    /// @endcode
    ///
    /// @tparam Key The key.
    /// @param value The value for `Key`.
    /// @return This view, carrying `value` for its parent layout.
    template <class Key>
    [[nodiscard]] View layout_value(typename Key::Value value) const {
        return modifier(LayoutValueModifier<Key>(std::move(value)));
    }

    // User extensions: a header of extra member declarations, set per target with
    // elegance_view_extensions() in CMake (design.md §9.4).
#ifdef ELG_VIEW_EXTENSIONS
#include ELG_VIEW_EXTENSIONS
#endif

private:
    friend struct detail::ViewAccess;

    ELG_API explicit View(std::shared_ptr<const detail::ViewNode> node) noexcept;
    [[nodiscard]] ELG_API View with_modifier(std::shared_ptr<const Modifier> modifier) const;
    [[nodiscard]] ELG_API View button_style_impl(std::shared_ptr<const ButtonStyle> style) const;
    [[nodiscard]] ELG_API View toggle_style_impl(std::shared_ptr<const ToggleStyle> style) const;
    [[nodiscard]] ELG_API View checkbox_style_impl(std::shared_ptr<const CheckboxStyle> style) const;
    [[nodiscard]] ELG_API View number_input_style_impl(std::shared_ptr<const NumberInputStyle> style) const;
    [[nodiscard]] ELG_API View progress_style_impl(std::shared_ptr<const ProgressStyle> style) const;
    [[nodiscard]] ELG_API View picker_style_impl(std::shared_ptr<const PickerStyle> style) const;
    [[nodiscard]] ELG_API View slider_style_impl(std::shared_ptr<const SliderStyle> style) const;
    [[nodiscard]] ELG_API View popover_style_impl(std::shared_ptr<const PopoverStyle> style) const;
    [[nodiscard]] ELG_API View tooltip_style_impl(std::shared_ptr<const TooltipStyle> style) const;
    [[nodiscard]] ELG_API View menu_style_impl(std::shared_ptr<const MenuStyle> style) const;
    [[nodiscard]] ELG_API View navigation_stack_style_impl(std::shared_ptr<const NavigationStackStyle> style) const;
    [[nodiscard]] ELG_API View text_field_style_impl(std::shared_ptr<const TextFieldStyle> style) const;
    [[nodiscard]] ELG_API View row_style_impl(std::shared_ptr<const RowStyle> style) const;
    [[nodiscard]] ELG_API View selection_impl(detail::SelectionModel model) const;
    [[nodiscard]] ELG_API View animation_impl(const Animation& animation, std::function<bool()> changed) const;
    ELG_API static View deferred(std::function<View()> body);

    std::shared_ptr<const detail::ViewNode> node_;
};

// Extensions may add member functions only; these catch the changes that would break the ABI.
static_assert(sizeof(View) == sizeof(std::shared_ptr<const detail::ViewNode>),
              "view extensions must not add data members");
static_assert(!std::is_polymorphic_v<View>, "view extensions must not add virtual functions");

} // namespace elg

#endif // ELG_VIEW_H
