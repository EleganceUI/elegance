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
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace elg {

namespace detail {
class ViewNode;
struct ViewAccess;

// For View::animation: a function that reads `value` (a tracked read) and returns whether it
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

// What a view is to screen readers, when the automatic choice isn't right (accessibility_role()).
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

// What a view does with things dragged in from other apps (on_drop()). Handlers get what was
// dropped and where (in the view's coordinates); returning false means it wasn't wanted.
struct DropOptions {
    std::function<bool(const std::vector<std::filesystem::path>& files, Point location)> files;
    std::function<bool(const std::string& text, Point location)> text;
    // Set while a drag the view can take is over it (for a highlight).
    std::function<void(bool targeted)> targeted;
};

// Where floating content (View::floating()) goes, by the view it's attached to (its anchor).
enum class FloatingEdge { bottom, top, trailing, leading };
// Across that edge: lined up with the anchor's start (leading or top), center, or end.
enum class FloatingAlignment { start, center, end };

struct FloatingPlacement {
    // The anchor's side it opens on. It flips to the opposite side when there isn't room.
    FloatingEdge edge = FloatingEdge::bottom;
    FloatingAlignment alignment = FloatingAlignment::start;
    float spacing = 0; // between the anchor and the content (negative: overlapping)
    Point offset;      // then moved by this
    // Instead of an edge: the content's top-leading corner here, in the anchor's coordinates.
    std::optional<Point> position;
};

// Where floating content may go.
enum class FloatingBoundary {
    // In a popup window of its own: it can extend past the app's window, and stays on the
    // display. Where the platform constrains popup windows (Wayland), the system has the last word.
    screen,
    // Drawn inside the app's window, above its content, and kept within it.
    window,
};

// What closes floating content (and sets its binding back to false).
struct FloatingDismiss {
    // A press anywhere outside it. While open, it then takes the pointer: presses elsewhere only
    // close it. false: the rest of the window stays usable.
    bool outside_click = true;
    bool escape = true; // Esc, unless something focused handles it first
};

// How popup content opens and closes: `transition` run with `animation` (elg/animation.h).
//   PopupMotion(Transition::opacity().combined(Transition::scale(0.96f, Alignment::top)),
//               Animation::ease_out(0.15))
class ELG_API PopupMotion {
public:
    PopupMotion(const Transition& transition, const Animation& animation);
    // At once, with no animation (the built-in styles' default).
    static PopupMotion none() { return PopupMotion(); }

    const Transition* transition() const noexcept { return transition_.get(); }
    const Animation* animation() const noexcept { return animation_.get(); }
    bool animates() const noexcept { return transition_ && animation_; }

private:
    PopupMotion() = default;
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<const Transition> transition_;
    std::shared_ptr<const Animation> animation_;
    ELG_PRIVATE_MEMBERS_END
};

struct FloatingOptions {
    FloatingPlacement placement;
    FloatingBoundary boundary = FloatingBoundary::screen;
    FloatingDismiss dismiss;
    std::optional<PopupMotion> motion; // nullopt: none
};

struct TooltipOptions {
    FloatingPlacement placement{.spacing = 4}; // below the view by default
    FloatingBoundary boundary = FloatingBoundary::screen;
    double delay = 0.6; // seconds the pointer rests on the view before it shows
    std::optional<PopupMotion> motion; // nullopt: the TooltipStyle's
};

struct PopoverOptions {
    // Centered below the view by default.
    FloatingPlacement placement{.alignment = FloatingAlignment::center, .spacing = 4};
    FloatingBoundary boundary = FloatingBoundary::screen;
    Prop<bool> padded = true; // false: the content reaches the panel's edges (images, lists)
    std::optional<PopupMotion> motion; // nullopt: the PopoverStyle's
};

// How content with an aspect ratio fills space (aspect_ratio()).
enum class ContentMode {
    fit,  // all of it shows: as large as fits
    fill, // it covers the space: as small as covers it (overflowing on one axis; clip it)
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

// Adjustments to what .backdrop_blur() shows through, after the blur. Each is reactive and
// animatable.
struct BackdropOptions {
    Prop<float> saturation = 1.f; // 0 gray, 1 unchanged, above 1 more vivid
    Prop<float> brightness = 0.f; // added to each channel: -1 black ... 0 unchanged ... 1 white
    Prop<float> noise = 0.f;      // grain, 0...1, so smooth gradients don't band
};

// A drop shadow (design.md §9.4): `radius` is the blur radius in points, (x, y) the offset.
// `spread` grows (or, negative, shrinks) the shape before blurring; it applies to single shapes
// (rects, rounded rects, capsules, circles), as in Figma, and is ignored for other content.
// Stack shadows by chaining: .shadow({.radius = 2, .y = 1}).shadow({.radius = 24, .y = 12}).
struct Shadow {
    Color color = Color::black.opacity(0.33f);
    float radius = 8;
    float x = 0;
    float y = 0;
    float spread = 0;
};

// How a view's pixels combine with what's behind them (design.md §9.4): the W3C compositing
// modes, as in CSS mix-blend-mode, Figma, and SwiftUI. Blending reads the window behind the view
// (or, inside an offscreen group, what the group drew behind it).
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
    // Whether this is an empty view (View(), EmptyView()), e.g. a style's label for a control
    // made without one.
    ELG_API bool is_empty() const noexcept;

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

    ELG_API View padding(Prop<float> length = 16.f) const;
    ELG_API View padding(Edges edges, Prop<float> length = 16.f) const;
    ELG_API View padding(EdgeInsets insets) const; // also takes {.top = 4, ...}
    ELG_API View padding(Prop<EdgeInsets> insets) const;

    ELG_API View frame(FrameOptions options) const;
    ELG_API View frame(float width, float height, Alignment alignment = Alignment::center) const;
    ELG_API View frame(FlexibleFrameOptions options) const;
    // A fixed size that follows state, like frame(width, height) but laid out again whenever the
    // size changes (a panel or window resized by dragging).
    //   content.frame([=] { return Size{width(), height()}; })
    ELG_API View frame(Prop<Size> size, Alignment alignment = Alignment::center) const;

    // Proposes nothing on the given axes, so the content takes its ideal size.
    ELG_API View fixed_size(bool horizontal = true, bool vertical = true) const;
    // Sizes the view to an aspect ratio (width / height) within what's proposed; without a
    // ratio, the view's own ideal size gives it (an image's).
    ELG_API View aspect_ratio(ContentMode mode) const;
    ELG_API View aspect_ratio(float ratio, ContentMode mode = ContentMode::fit) const;
    // Raster Images: take whatever size they're offered instead of their own (stretching to
    // it; combine with aspect_ratio to keep proportions). Throws elg::UsageError on other views.
    ELG_API View resizable() const;
    // Stacks give space to higher priorities first; in a ZStack, the highest-priority children
    // decide its size and the others are offered that size. Default 0.
    ELG_API View layout_priority(float priority) const;
    // Stacking order among siblings (any stack, not just ZStack): higher draws in front and is hit
    // first. Default 0; equal values keep declaration order (later in front).
    ELG_API View z_index(Prop<float> z) const;
    // Moves the content when drawn, without affecting layout (and animates the move during
    // animated updates). Reactive: .offset([=] { return open() ? 0.f : -200.f; }, 0)
    ELG_API View offset(Prop<float> x, Prop<float> y) const;

    // --- Grids (elg/grid.h) ---

    // A Grid cell that spans `count` columns.
    ELG_API View grid_cell_columns(int count) const;
    // Where this view sits in its grid cell, instead of its row's, column's, or grid's alignment.
    ELG_API View grid_cell_alignment(Alignment alignment) const;
    // The horizontal alignment of every cell in this cell's Grid column.
    ELG_API View grid_column_alignment(HorizontalAlignment alignment) const;

    // --- Decoration (elg/modifiers/decoration.h) ---
    // The shorthands are exactly their compositions, drawn and hit tested the same way:
    //   .background(style)        == .background(Rectangle().fill(style))
    //   .background(style, shape) == .background(shape.fill(style))
    //   .border(color, width)     == .overlay(Rectangle().stroke_border(color, width))
    //   .corner_radius(r)         == .clip_shape(RoundedRectangle(r))
    // A shape takes the same points whether it's filled, a clip_shape or a content_shape; a
    // stroke-only shape takes only its line.

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
    // A shadow cast inside the view's content, from its edges inward (pressed-in, embossed).
    ELG_API View inner_shadow(Shadow shadow = {.color = Color::black.opacity(0.25f), .radius = 6, .y = 2}) const;
    // Gaussian blur of the content; `radius` in points. Reactive and animatable.
    // `opaque`: the content fills its bounds; edges stay sharp instead of fading out.
    ELG_API View blur(Prop<float> radius, bool opaque = false) const;
    // Blurs what's drawn behind the view in the window (frosted glass), inside `shape` (the
    // view's bounds by default); `radius` in points. Reactive and animatable. Like .background,
    // it goes behind everything it wraps, so a tint goes on before it:
    //   Text("Now playing").padding(12)
    //       .background(Color::white.opacity(0.5f), RoundedRectangle(12))
    //       .backdrop_blur(30, RoundedRectangle(12), {.saturation = 1.8f, .noise = 0.02f})
    // .blur() after it blurs the view, backdrop included. Inside an offscreen group (opacity below
    // 1, a shadow, a blur, ...) it sees only what the group drew behind it, as CSS's
    // backdrop-filter does.
    ELG_API View backdrop_blur(Prop<float> radius, BackdropOptions options = {}) const;
    ELG_API View backdrop_blur(Prop<float> radius, const Shape& shape, BackdropOptions options = {}) const;
    // Color adjustments (SwiftUI's, and CSS's filter functions). Each is reactive and animatable.
    // Adds to each channel: -1 black ... 0 unchanged ... 1 white.
    ELG_API View brightness(Prop<float> amount) const;
    // 0 flat gray, 1 unchanged, above 1 more contrast.
    ELG_API View contrast(Prop<float> amount) const;
    // 0 gray, 1 unchanged, above 1 more vivid.
    ELG_API View saturation(Prop<float> amount) const;
    // 0 unchanged ... 1 fully gray.
    ELG_API View grayscale(Prop<float> amount) const;
    // Rotates hues around the color wheel by `degrees`.
    ELG_API View hue_rotation(Prop<float> degrees) const;
    // 0 unchanged ... 1 fully sepia-toned.
    ELG_API View sepia(Prop<float> amount) const;
    // Inverts the colors, or partly (0...1).
    ELG_API View color_invert() const;
    ELG_API View color_invert(Prop<float> amount) const;
    // Multiplies every pixel by `color` (alpha too): tints white content, darkens the rest.
    ELG_API View color_multiply(Prop<Color> color) const;
    // Brightness becomes opacity: white stays, black disappears (for masks).
    ELG_API View luminance_to_alpha() const;
    // Blends the view with what's behind it.
    ELG_API View blend_mode(BlendMode mode) const;
    // Draws the content offscreen first, then composites it as one image: blend modes inside it
    // blend with each other only, and a material inside sees only what the group drew.
    ELG_API View compositing_group() const;
    // Rotates the drawn content by `degrees` (clockwise) about `anchor`, without affecting layout
    // or hit testing. Reactive and animatable.
    ELG_API View rotation_effect(Prop<float> degrees, UnitPoint anchor = UnitPoint::center) const;
    // Scales the drawn content about `anchor`, without affecting layout or hit testing. A uniform
    // scale stays sharp (vector); an uneven one renders offscreen first. Reactive and animatable.
    ELG_API View scale_effect(Prop<float> scale, UnitPoint anchor = UnitPoint::center) const;
    ELG_API View scale_effect(Prop<float> x, Prop<float> y, UnitPoint anchor = UnitPoint::center) const;
    // Shows the content only where `mask` (sized and aligned like an overlay) is opaque; its
    // colors don't matter, only its alpha. A gradient fades the content out:
    //   list.mask(LinearGradient({Color::black, Color::clear}, UnitPoint::center, UnitPoint::bottom))
    ELG_API View mask(View mask, Alignment alignment = Alignment::center) const;
    ELG_API View mask(Prop<ShapeStyle> style) const;
    // Hit tests against this shape (filling the bounds) instead of the drawn content, e.g.
    // content_shape(Rectangle()) makes the gaps in a stack tappable. A Button takes its whole
    // frame; .content_shape(Capsule()) narrows it to its shape.
    ELG_API View content_shape(const Shape& shape) const;
    // false: the view and everything in it ignore the pointer; events go to what's behind.
    ELG_API View allows_hit_testing(bool enabled) const;

    // --- Events (elg/modifiers/events.h) ---

    // Pointer clicks: pressed and released inside the view with the options' button (left by
    // default) and click count. The innermost click handler wins. Clicks only: for something a
    // keyboard or a screen reader should be able to use too, use on_activate.
    //   Canvas(draw).on_click([=](const ClickEvent& e) { add_point(e.location); })
    //   Row().on_click(select).on_double_click(open)
    ELG_API View on_click(std::function<void(const ClickEvent&)> action, ClickOptions options = {}) const;
    ELG_API View on_click(std::function<void()> action, ClickOptions options = {}) const;
    // on_click with .count = 2.
    ELG_API View on_double_click(std::function<void(const ClickEvent&)> action) const;
    ELG_API View on_double_click(std::function<void()> action) const;
    // The view's action, however it's used: a left click, Space or Enter while it has focus, or a
    // screen reader's default action. Makes it focusable (Tab, with a focus ring); its
    // accessibility role stays its own. Disabled views (.disabled) don't activate.
    //   Tile(album).on_activate([=] { open(album); }).accessibility_label(album.title)
    ELG_API View on_activate(std::function<void()> action) const;
    // Gestures (elg/gestures.h): drags and long presses take a press over once they recognize it
    // (a button or on_click under them then doesn't fire); pinches, rotations and smart zoom come
    // from trackpads.
    //   .recognize(DragGesture().on_began([=] { start = offset.peek(); })
    //                           .on_changed([=](const DragEvent& e) { offset = start() + e.translation; }))
    ELG_API View recognize(DragGesture gesture) const;
    ELG_API View recognize(LongPressGesture gesture) const;
    ELG_API View recognize(MagnifyGesture gesture) const;
    ELG_API View recognize(RotateGesture gesture) const;
    ELG_API View recognize(SmartMagnifyGesture gesture) const;
    // Shorthands: recognize(DragGesture().on_changed(action)), recognize(LongPressGesture(action)),
    // recognize(MagnifyGesture().on_changed(action)), ...
    ELG_API View on_drag(std::function<void(const DragEvent&)> action) const;
    ELG_API View on_long_press(std::function<void()> action) const;
    ELG_API View on_magnify(std::function<void(const MagnifyEvent&)> action) const;
    ELG_API View on_rotate(std::function<void(const RotateEvent&)> action) const;
    ELG_API View on_smart_magnify(std::function<void(Point)> action) const;
    // The pointer's shape over the view, like CSS's `cursor`: .pointer_style(PointerStyle::pointer)
    // over something to click. Views inside can set their own (a text field's I-beam). Reactive:
    // .pointer_style([=] { return busy() ? PointerStyle::wait : PointerStyle::automatic; })
    ELG_API View pointer_style(Prop<PointerStyle> style) const;
    // Called at once when the pointer comes over the view (its hit area) and when it leaves: for
    // highlights.
    //   .on_pointer_enter([=] { hovered = true; }).on_pointer_leave([=] { hovered = false; })
    ELG_API View on_pointer_enter(std::function<void()> action) const;
    ELG_API View on_pointer_leave(std::function<void()> action) const;
    // Raw pointer events over the view (down, up, moves while captured, hover moves, cancel),
    // located in its own coordinates. Return true to consume one; consuming a down captures the
    // pointer, so the moves and the up come here wherever it goes. For drawing and custom
    // controls; prefer on_click and gestures otherwise.
    ELG_API View on_pointer(std::function<bool(const PointerEvent&)> action) const;
    // Wheel and trackpad scrolling over the view, located in its own coordinates. Return true to
    // consume it (scroll views around it then don't scroll).
    ELG_API View on_scroll(std::function<bool(const ScrollEvent&)> action) const;
    // Hover intent: called with true once the pointer has rested on the view for the hover delay
    // (moving across it doesn't count), and false when it leaves (after the exit delay): for
    // previews and other things that shouldn't flash as the pointer passes. The delay comes from
    // the environment (.hover_delay()) unless given.
    ELG_API View on_hover_change(std::function<void(bool)> action) const;
    ELG_API View on_hover_change(std::function<void(bool)> action, HoverDelay delay) const;
    // Called after initial layout and when physical dimensions change, including display scale.
    // Dimensions are ceil(logical size * display scale), before visual transforms, and may be zero.
    // Runs on the UI thread before paint: queue renderer input, do not mutate layout here.
    ELG_API View on_size_change(std::function<void(PixelSize)> action) const;
    // Files or text dragged from other apps (Explorer, a browser) can be dropped here. The
    // innermost view under the pointer that takes that kind of thing gets it.
    ELG_API View on_drop(DropOptions options) const;

    // --- Accessibility (design.md §12) ---
    // Built-in views describe themselves to screen readers; these adjust that.
    // What it's called (an icon-only button needs one: Image("trash") says nothing).
    ELG_API View accessibility_label(Prop<std::string> label) const;
    // What it holds, as words ("3 of 10", "50%").
    ELG_API View accessibility_value(Prop<std::string> value) const;
    // What happens if it's used, when the label doesn't say ("Deletes the draft").
    ELG_API View accessibility_hint(Prop<std::string> hint) const;
    // Leaves it (and everything inside) out: decoration.
    ELG_API View accessibility_hidden(bool hidden = true) const;
    ELG_API View accessibility_role(AccessibilityRole role) const;
    // One element for everything inside, labeled with their text (a row read as one line).
    ELG_API View accessibility_element() const;
    // What a screen reader's default action (click) does, for views made clickable with gestures.
    ELG_API View accessibility_action(std::function<void()> action) const;
    // The hover delay for on_hover_change handlers and submenus inside (default: enter 0.1 s,
    // exit 0). Controls' highlights and on_pointer_enter/leave don't wait.
    ELG_API View hover_delay(HoverDelay delay) const;

    // Pressing and dragging here moves the window (for custom title bars, TitleBar::hidden).
    // Double-clicking maximizes, like a system title bar. Controls inside still work.
    ELG_API View window_drag_area(bool enabled = true) const;

    // --- Popups (design.md §13.1) ---

    // Right-click (or Shift+F10 / the Menu key while focused) opens the menu `menu` builds,
    // usually a Menu(...). The builder runs each time it opens; item actions run after it closes.
    //   Text(file.name).context_menu([=] { return Menu(MenuItem("Rename", rename), Separator(),
    //                                                  MenuItem("Delete", remove, {.destructive = true})); })
    ELG_API View context_menu(std::function<View()> menu) const;
    // A tooltip: `content` (in the environment's TooltipStyle) shown after the pointer rests on
    // the view for a moment, until it leaves or presses. Screen readers don't see it.
    //   Avatar(user).tooltip([=] { return HStack(Image("user"), Text(user.name)); })
    ELG_API View tooltip(std::function<View()> content, TooltipOptions options = {}) const;
    // A text tooltip: tooltip(Text(text)).
    ELG_API View help(Prop<std::string> text) const;
    // `content` floating above everything by the view, while `is_presented` is true, with no
    // chrome of its own. The builder runs each time it opens. What closes it (and sets the
    // binding back to false), where it goes and whether it may leave the window are `options`'.
    //   Avatar().floating(card_shown.binding(), [=] { return ProfileCard(user); },
    //                     {.placement = {.edge = FloatingEdge::trailing, .spacing = 8},
    //                      .boundary = FloatingBoundary::window})
    ELG_API View floating(Binding<bool> is_presented, std::function<View()> content,
                          FloatingOptions options = {}) const;
    // A panel of `content` beside the view, shown while `is_presented` is true. Clicking outside
    // it, Esc, or switching apps closes it and sets `is_presented` back to false. The builder runs
    // each time it opens; the content can be anything, e.g. a small form.
    //   Button("Details", [=] { showing = true; })
    //       .popover(showing.binding(), [=] { return VStack(Text("Release notes"), ...); })
    ELG_API View popover(Binding<bool> is_presented, std::function<View()> content,
                         PopoverOptions options = {}) const;

    // --- Keyboard and focus (design.md §10) ---

    // true: the view takes keyboard focus (Tab order, clicks); controls are focusable already.
    // false: the view and everything inside it never take focus (Tab skips them, clicking them
    // leaves focus where it was), e.g. toolbar or caption buttons.
    ELG_API View focusable(bool focusable = true) const;
    // Two-way focus: the binding is true while the view has focus, and setting it true focuses
    // the view (false removes focus from it). Makes the view focusable.
    ELG_API View focused(Binding<bool> is_focused) const;
    // Runs `action` when `key` is pressed while the view (or something inside) has focus.
    ELG_API View on_key_press(Key key, std::function<void()> action) const;
    // Sees every key down and up while the view (or something inside) has focus; return true if
    // handled (unhandled keys bubble out).
    ELG_API View on_key(std::function<bool(const KeyEvent&)> action) const;
    // Makes the view focusable and sees the text typed while it has focus: commits, and an IME's
    // compositions in progress. Keys still go to on_key. Return true if handled.
    ELG_API View on_text_input(std::function<bool(const TextInputEvent&)> action) const;

    // --- Environment (elg/modifiers/environment.h): inherited by everything inside. ---

    // The weight of the font inside, keeping its size and family (text, and icons of sets with
    // weights): Image("gear").font_weight(Weight::bold).
    ELG_API View font_weight(Weight weight) const;
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
    // The style of every Toggle inside (elg/styles.h).
    template <class Style>
        requires std::derived_from<Style, ToggleStyle>
    View toggle_style(Style style) const {
        return toggle_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    // The style of every Checkbox inside (elg/styles.h).
    template <class Style>
        requires std::derived_from<Style, CheckboxStyle>
    View checkbox_style(Style style) const {
        return checkbox_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    // The style of every NumberInput inside (elg/styles.h).
    template <class Style>
        requires std::derived_from<Style, NumberInputStyle>
    View number_input_style(Style style) const {
        return number_input_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    // The styles of popovers, tooltips and menus opened from inside (elg/styles.h).
    template <class Style>
        requires std::derived_from<Style, PopoverStyle>
    View popover_style(Style style) const {
        return popover_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    template <class Style>
        requires std::derived_from<Style, TooltipStyle>
    View tooltip_style(Style style) const {
        return tooltip_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    template <class Style>
        requires std::derived_from<Style, MenuStyle>
    View menu_style(Style style) const {
        return menu_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    // The style of every Progress inside (elg/styles.h).
    template <class Style>
        requires std::derived_from<Style, ProgressStyle>
    View progress_style(Style style) const {
        return progress_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    // The style of every NavigationStack inside (elg/navigation.h).
    template <class Style>
        requires std::derived_from<Style, NavigationStackStyle>
    View navigation_stack_style(Style style) const {
        return navigation_stack_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    // The style of every TextField inside (elg/styles.h).
    template <class Style>
        requires std::derived_from<Style, TextFieldStyle>
    View text_field_style(Style style) const {
        return text_field_style_impl(std::make_shared<const Style>(std::move(style)));
    }
    // --- Animation (elg/animation.h) ---

    // Changes inside this view animate with `animation` whenever `value` (a plain value, State,
    // or closure; it should be equality-comparable) changed in the same update.
    template <class Value>
    View animation(const Animation& animation, Value value) const {
        return animation_impl(animation, detail::change_watcher(std::move(value)));
    }
    // How this view enters and leaves when an If/ForEach adds or removes it with an animation.
    ELG_API View transition(const Transition& transition) const;
    // false: this view and everything inside snap to new positions and sizes instead of easing
    // there. Colors, opacity, and transitions still animate.
    ELG_API View animates_layout(bool enabled) const;

    // Sets any environment key (elg/environment.h): .environment<CompactKey>(true)
    template <class Key, class Value>
    View environment(Value&& value) const {
        return modifier(EnvironmentKeyModifier<Key>(std::forward<Value>(value)));
    }

    // A value for the parent layout to read with LayoutChild::value<Key>() (custom layouts,
    // elg/layout.h). A key is a type with a `Value` type and a `default_value()`:
    //   struct Rank { using Value = int; static int default_value() { return 0; } };
    //   Text("Gold").layout_value<Rank>(1)
    template <class Key>
    View layout_value(typename Key::Value value) const {
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
    ELG_API View with_modifier(std::shared_ptr<const Modifier> modifier) const;
    ELG_API View button_style_impl(std::shared_ptr<const ButtonStyle> style) const;
    ELG_API View toggle_style_impl(std::shared_ptr<const ToggleStyle> style) const;
    ELG_API View checkbox_style_impl(std::shared_ptr<const CheckboxStyle> style) const;
    ELG_API View number_input_style_impl(std::shared_ptr<const NumberInputStyle> style) const;
    ELG_API View progress_style_impl(std::shared_ptr<const ProgressStyle> style) const;
    ELG_API View popover_style_impl(std::shared_ptr<const PopoverStyle> style) const;
    ELG_API View tooltip_style_impl(std::shared_ptr<const TooltipStyle> style) const;
    ELG_API View menu_style_impl(std::shared_ptr<const MenuStyle> style) const;
    ELG_API View navigation_stack_style_impl(std::shared_ptr<const NavigationStackStyle> style) const;
    ELG_API View text_field_style_impl(std::shared_ptr<const TextFieldStyle> style) const;
    ELG_API View animation_impl(const Animation& animation, std::function<bool()> changed) const;
    ELG_API static View deferred(std::function<View()> body);

    std::shared_ptr<const detail::ViewNode> node_;
};

// Extensions may add member functions only; these catch the changes that would break the ABI.
static_assert(sizeof(View) == sizeof(std::shared_ptr<const detail::ViewNode>),
              "view extensions must not add data members");
static_assert(!std::is_polymorphic_v<View>, "view extensions must not add virtual functions");

} // namespace elg

#endif // ELG_VIEW_H
