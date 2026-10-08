// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_VIEW_H
#define ELG_VIEW_H

#include <elg/color.h>
#include <elg/export.h>
#include <elg/geometry.h>

#include <concepts>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace elg {

namespace detail {
class ViewNode;
struct ViewAccess;
} // namespace detail

class ButtonStyle;
class Font;
class Modifier;
class Shape;
class ShapeStyle;
struct Theme;
enum class ColorScheme;
template <class T>
class Prop;
template <class Key>
class EnvironmentKeyModifier;

inline constexpr float infinity = std::numeric_limits<float>::infinity();

enum class Axis { horizontal, vertical };

enum class HorizontalAlignment { leading, center, trailing };
enum class VerticalAlignment { top, center, bottom };

struct Alignment {
    HorizontalAlignment horizontal = HorizontalAlignment::center;
    VerticalAlignment vertical = VerticalAlignment::center;

    static const Alignment center;
    static const Alignment top;
    static const Alignment bottom;
    static const Alignment leading;
    static const Alignment trailing;
    static const Alignment top_leading;
    static const Alignment top_trailing;
    static const Alignment bottom_leading;
    static const Alignment bottom_trailing;

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

// A size offered by a parent during layout (design.md §7). Each dimension is optional:
// nullopt asks for the ideal size, 0 for the minimum, and infinity for the maximum.
struct ProposedSize {
    std::optional<float> width;
    std::optional<float> height;

    static constexpr ProposedSize unspecified() noexcept { return {}; }
    static constexpr ProposedSize zero() noexcept { return {0.f, 0.f}; }
    static constexpr ProposedSize infinite() noexcept { return {infinity, infinity}; }

    constexpr Size replacing_unspecified(Size fallback) const noexcept {
        return {width.value_or(fallback.width), height.value_or(fallback.height)};
    }

    friend constexpr bool operator==(const ProposedSize&, const ProposedSize&) = default;
};

struct EdgeInsets {
    float top = 0;
    float leading = 0;
    float bottom = 0;
    float trailing = 0;

    static constexpr EdgeInsets all(float length) noexcept { return {length, length, length, length}; }
    static constexpr EdgeInsets symmetric(float horizontal, float vertical) noexcept {
        return {vertical, horizontal, vertical, horizontal};
    }
};

enum class Edges : unsigned {
    top = 1,
    leading = 2,
    bottom = 4,
    trailing = 8,
    horizontal = leading | trailing,
    vertical = top | bottom,
    all = top | leading | bottom | trailing,
};

constexpr Edges operator|(Edges a, Edges b) noexcept {
    return static_cast<Edges>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

constexpr bool contains(Edges set, Edges edge) noexcept {
    return (static_cast<unsigned>(set) & static_cast<unsigned>(edge)) != 0;
}

// A drop shadow (design.md §9.4): `radius` is the blur radius in points, (x, y) the offset.
struct Shadow {
    Color color = Color::black.opacity(0.33f);
    float radius = 8;
    float x = 0;
    float y = 0;
};

// An optional length in points, for option structs. Unlike std::optional<float>, it takes any
// number ({.spacing = 12}, {.width = 12.5}) without narrowing warnings.
class OptionalLength {
public:
    constexpr OptionalLength() noexcept = default;
    constexpr OptionalLength(std::nullopt_t) noexcept {}
    constexpr OptionalLength(std::optional<float> length) noexcept : value_(length) {}
    template <class T>
        requires(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
    constexpr OptionalLength(T length) noexcept : value_(static_cast<float>(length)) {}

    constexpr bool has_value() const noexcept { return value_.has_value(); }
    constexpr explicit operator bool() const noexcept { return value_.has_value(); }
    constexpr float operator*() const noexcept { return *value_; }
    constexpr float value_or(float fallback) const noexcept { return value_.value_or(fallback); }
    constexpr operator std::optional<float>() const noexcept { return value_; }

private:
    std::optional<float> value_;
};

// .frame({.width = 100}): fixed dimensions; unset ones follow the content.
struct FrameOptions {
    OptionalLength width;
    OptionalLength height;
    Alignment alignment = Alignment::center;
};

// .frame({.max_width = infinity}): a size range; the frame takes the proposed size, clamped.
struct FlexibleFrameOptions {
    OptionalLength min_width;
    OptionalLength ideal_width;
    OptionalLength max_width;
    OptionalLength min_height;
    OptionalLength ideal_height;
    OptionalLength max_height;
    Alignment alignment = Alignment::center;
};

// A description of UI: an immutable, cheaply copyable value. Nothing is created until a view
// is mounted in a window (design.md §9.2).
//
// Every modifier method is sugar for View::modifier() with a public modifier type
// (design.md §9.4), e.g. padding(8) == modifier(PaddingModifier(EdgeInsets::all(8))).
//
// Members are exported one by one rather than the whole class: a dllimport class can't have
// members defined outside the DLL, and user extensions (below) are exactly that.
class View {
public:
    // An empty view: takes no space and draws nothing.
    ELG_API View() noexcept;

    // Components: any type with `View body() const` can be used as a view.
    template <class Component>
        requires(!std::derived_from<std::remove_cvref_t<Component>, View> &&
                 requires(const Component& c) {
                     { c.body() } -> std::convertible_to<View>;
                 })
    View(Component component)
        : View(deferred([component = std::move(component)] { return View(component.body()); })) {}

    // --- The one entry point for modifiers; built-in and user modifiers alike. ---

    template <std::derived_from<Modifier> M>
    View modifier(M modifier) const {
        return with_modifier(std::make_shared<const M>(std::move(modifier)));
    }

    // Function-style helpers: v.apply(card) == card(v).
    template <std::invocable<View> F>
    View apply(F&& function) const {
        return std::invoke(std::forward<F>(function), *this);
    }

    // --- Layout (elg/modifiers/layout.h) ---

    ELG_API View padding(float length = 16) const;
    ELG_API View padding(Edges edges, float length = 16) const;
    ELG_API View padding(EdgeInsets insets) const;

    ELG_API View frame(FrameOptions options) const;
    ELG_API View frame(float width, float height, Alignment alignment = Alignment::center) const;
    ELG_API View frame(FlexibleFrameOptions options) const;

    // Proposes nothing on the given axes, so the content takes its ideal size.
    ELG_API View fixed_size(bool horizontal = true, bool vertical = true) const;
    // Stacks give space to higher priorities first. Default 0.
    ELG_API View layout_priority(float priority) const;
    // Moves the content when drawn without affecting layout.
    ELG_API View offset(float x, float y) const;

    // --- Decoration (elg/modifiers/decoration.h) ---

    ELG_API View background(View view, Alignment alignment = Alignment::center) const;
    // A color or gradient filling the view's bounds, or the given shape. Reactive arguments work:
    // .background([=] { return selected() ? accent : surface; })
    ELG_API View background(Prop<ShapeStyle> style) const;
    ELG_API View background(Prop<ShapeStyle> style, const Shape& shape) const;
    ELG_API View overlay(View view, Alignment alignment = Alignment::center) const;
    // A border drawn inside the view's bounds.
    ELG_API View border(Color color, float width = 1) const;

    // --- Effects (elg/modifiers/effects.h) ---

    // Clips the content to the view's bounds.
    ELG_API View clipped() const;
    // Clips the content to a shape filling the view's bounds (cheap for rounded rects/circles).
    ELG_API View clip_shape(const Shape& shape) const;
    // Shorthand for clip_shape(RoundedRectangle(radius)).
    ELG_API View corner_radius(float radius) const;
    // Fades the view as a group: overlapping content doesn't show through itself.
    ELG_API View opacity(Prop<float> opacity) const;
    // A blurred shadow of the view's content.
    ELG_API View shadow(Shadow shadow = {}) const;
    // Hit tests against this shape (filling the bounds) instead of the drawn content, e.g.
    // content_shape(Rectangle()) makes the gaps in a stack tappable.
    ELG_API View content_shape(const Shape& shape) const;
    // false: the view and everything in it ignore the pointer; events go to what's behind.
    ELG_API View allows_hit_testing(bool enabled) const;

    // --- Events (elg/modifiers/events.h) ---

    // Runs `action` on a tap (`count` = 2 for a double tap). The innermost tap gesture wins.
    ELG_API View on_tap_gesture(std::function<void()> action, int count = 1) const;
    // Called with true when the pointer enters the view, false when it leaves.
    ELG_API View on_hover(std::function<void(bool)> action) const;

    // --- Environment (elg/modifiers/environment.h): inherited by everything inside. ---

    ELG_API View font(Prop<Font> font) const;
    // The color of text and unfilled shapes. Also takes a ThemeColor.
    ELG_API View foreground_style(Prop<Color> color) const;
    // The accent color of controls (e.g. Button).
    ELG_API View tint(Prop<Color> color) const;
    // Light or dark, for ThemeColor tokens inside. Windows follow the system setting by default.
    ELG_API View color_scheme(Prop<ColorScheme> scheme) const;
    ELG_API View theme(Theme theme) const;
    // Controls inside stop responding and look disabled.
    ELG_API View disabled(Prop<bool> disabled) const;
    ELG_API View disabled() const;
    // The style of every Button inside (elg/styles.h).
    template <class Style>
        requires std::derived_from<Style, ButtonStyle>
    View button_style(Style style) const {
        return button_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    // Sets any environment key (elg/environment.h): .environment<CompactKey>(true)
    template <class Key, class Value>
    View environment(Value&& value) const {
        return modifier(EnvironmentKeyModifier<Key>(std::forward<Value>(value)));
    }

    // User extensions: a header of extra member declarations, set per target with
    // elegence_view_extensions() in CMake (design.md §9.4).
#ifdef ELG_VIEW_EXTENSIONS
#include ELG_VIEW_EXTENSIONS
#endif

private:
    friend struct detail::ViewAccess;

    ELG_API explicit View(std::shared_ptr<const detail::ViewNode> node) noexcept;
    ELG_API View with_modifier(std::shared_ptr<const Modifier> modifier) const;
    ELG_API View button_style_impl(std::shared_ptr<const ButtonStyle> style) const;
    ELG_API static View deferred(std::function<View()> body);

    std::shared_ptr<const detail::ViewNode> node_;
};

// Extensions may add member functions only; these catch the changes that would break the ABI.
static_assert(sizeof(View) == sizeof(std::shared_ptr<const detail::ViewNode>),
              "view extensions must not add data members");
static_assert(!std::is_polymorphic_v<View>, "view extensions must not add virtual functions");

} // namespace elg

#endif // ELG_VIEW_H
