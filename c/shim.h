// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Elegance's C API: what the Rust bindings (elegance-rs's elegance-sys) and other languages call.
// It lives here so a change to the C++ API updates it in the same pull request: every build
// compiles it (ELG_C_API), and c/CMakeLists.txt builds it with Elegance into the prebuilt shared
// library that CI publishes (.github/workflows/c-api.yml). elegance-sys declares the same functions
// and structs in Rust, and its tests check the struct layouts (elg_layouts_*).
//
// Every function is `extern "C"` and noexcept in practice: C++ exceptions are caught and recorded
// for elg_take_error(). Rust closures arrive as ElgClosure; a closure that panicked returns false,
// and the shim turns that into a RustPanic exception so Elegance unwinds as it does for its own
// errors.
//
// Ownership: an `elg::View*` (and Text*, Shape*, ...) argument named `take_*` or passed in an
// array is consumed. `const T*` arguments are borrowed. Returned pointers are owned by the caller.

#ifndef ELG_RUST_SHIM_H
#define ELG_RUST_SHIM_H

#include <elg/elegance.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// ELG_SHIM_SHARED: the shim and Elegance built into one shared library (c/CMakeLists.txt, the
// prebuilt library), which exports just these functions and ELG_API.
#if defined(ELG_SHIM_SHARED) && defined(_WIN32)
#define ELG_EXPORT extern "C" __declspec(dllexport)
#elif defined(ELG_SHIM_SHARED)
#define ELG_EXPORT extern "C" __attribute__((visibility("default")))
#else
#define ELG_EXPORT extern "C"
#endif

extern "C" {

/// UTF-8 text: `len` bytes at `ptr`, not NUL-terminated. A null `ptr` is the empty string.
/// Passed to Elegance, it's read during the call (Elegance copies what it keeps). Passed to a
/// callback or returned, it points into Elegance's memory: valid during the callback, or as long
/// as the function says.
struct ElgStr {
    const char* ptr; ///< The first byte, or null.
    size_t len;      ///< Length in bytes.
};

/// Bytes: `len` of them at `ptr`. Read during the call or valid during the callback, like ElgStr.
/// A null `ptr` is no bytes.
struct ElgBytes {
    const unsigned char* ptr; ///< The first byte, or null.
    size_t len;               ///< How many bytes.
};

/// Calls a Rust closure. `args` and `ret` point at values whose types depend on the call site.
///
/// @param data The closure's ElgClosure::data.
/// @param args The closure's argument, or null. Each function's doc says what it points to.
/// @param ret Where the closure writes its result, or null. Each function's doc says what it
///   points to.
/// @return false if the closure panicked.
typedef bool (*ElgCallFn)(void* data, void* args, void* ret);

/// Frees a Rust closure that Elegance owns, once Elegance is done with it.
///
/// @param data The closure's ElgClosure::data.
typedef void (*ElgDropFn)(void* data);

/// A Rust closure. `call` null: no closure (optional callbacks). `drop` null: borrowed.
///
/// A function either consumes a closure (Elegance calls `drop` once it's done with it) or borrows
/// it (calls it only before returning, and never drops it). Each function's doc says which, and
/// what `args` and `ret` point to.
struct ElgClosure {
    void* data;     ///< The closure's state, passed to `call` and `drop`.
    ElgCallFn call; ///< Runs the closure; null: no closure.
    ElgDropFn drop; ///< Frees `data`; null: nothing to free.
};

/// Receives a string from Elegance (an error message, a font family, a device's error).
///
/// @param ctx The `ctx` the function was given along with the sink.
/// @param ptr The string's UTF-8 bytes, not NUL-terminated, valid only during the call.
/// @param len Length in bytes.
typedef void (*ElgStrSink)(void* ctx, const char* ptr, size_t len);

/// An elg::Color: sRGB with straight (non-premultiplied) alpha.
struct ElgColor {
    float r, g, b, a; ///< Red, green, blue, and alpha (opacity), each 0 to 1.
};

/// A width and height (elg::Size), in points unless a function says pixels.
struct ElgSize {
    float width, height; ///< Width and height.
};

/// An elg::PixelSize: physical dimensions, unlike the logical points of ElgSize.
struct ElgPixelSize {
    uint32_t width, height; ///< Width and height in pixels.
};

/// Wraps `View::on_size_change()`: calls `on_size_change` after the first layout and whenever the
/// view's size in pixels changes (the display scale included), on the UI thread before paint.
///
/// @param v The view to modify (borrowed).
/// @param on_size_change Consumed closure; args: `ElgPixelSize*`, ret: null. Callback ownership is
///   transferred to the view. The size is ceil(logical size * display scale), before visual
///   transforms, and may be zero.
/// @return The modified view, owned by the caller, or nullptr on failure.
ELG_EXPORT elg::View* elg_mod_on_size_change(const elg::View* v, ElgClosure on_size_change);

/// An elg::Point in points, y growing downward. Where a function says so, a UnitPoint instead:
/// (0, 0) is a view's top-leading corner, (1, 1) its bottom-trailing one.
struct ElgPoint {
    float x, y; ///< Horizontal and vertical position.
};

/// An elg::Rect, in points.
struct ElgRect {
    float x, y, width, height; ///< Top-left corner and size.
};

/// An elg::Font.
struct ElgFont {
    ElgStr family;  ///< A family registered with register_font(); empty: the system font.
    float size;     ///< Size in points.
    int32_t weight; ///< An elg::Weight, 1 to 1000: 400 regular, 700 bold.
    bool italic;    ///< The italic face.
};

/// An elg::GradientStop.
struct ElgGradientStop {
    ElgColor color; ///< The color at this stop.
    float location; ///< Where along the gradient, 0 to 1.
};

/// An elg::ShapeStyle: a color or a gradient. Points are unit points of the filled shape's bounds.
///
/// kind 0: color; 1: linear gradient from (x0, y0) to (x1, y1); 2: radial gradient around (x0, y0)
/// from radius r0 to r1; 3: elliptical gradient around (x0, y0) from radius fraction r0 to r1;
/// 4: angular gradient around (x0, y0) from angle r0 to r1 (degrees).
struct ElgShapeStyle {
    int32_t kind;                 ///< 0 to 4, as above. Other values are a color.
    ElgColor color;               ///< Kind 0: the color.
    const ElgGradientStop* stops; ///< Gradients: the color stops, read during the call.
    size_t stop_count;            ///< Number of `stops`.
    float x0, y0, x1, y1;         ///< Kind 1: the start and end points. Kinds 2 to 4: the center (x0, y0).
    /// Kinds 2 to 4: the start and end radius in points (radial), as a fraction of the bounds' size
    /// (elliptical; 0.5 reaches the edges), or the angles in degrees, clockwise from trailing (angular).
    float r0, r1;
};

/// An elg::EdgeInsets, in points.
struct ElgEdgeInsets {
    float top, leading, bottom, trailing; ///< The inset on each side.
};

/// A reactive argument (an elg::Prop). kind 0: `value` points at the static value (its C form);
/// 1: `fn` returns it (ret: a C++ T*); 2: `fn` takes the environment (args: const
/// EnvironmentValues*); 3: the theme color `theme` (Color and ShapeStyle props).
struct ElgProp {
    int32_t kind;      ///< 0 to 3, as above.
    int32_t theme;     ///< Kind 3: an elg::ThemeColor. Props of other types get their default value.
    /// Kind 0: the value's C form (`float*`, `bool*`, `ElgStr*`, `ElgColor*`, ...), read during the
    /// call.
    const void* value;
    /// Kinds 1 and 2: consumed; runs whenever Elegance reads the prop, and writes the C++ value
    /// into `ret` (strings with elg_string_assign()). Other kinds ignore it and don't drop it.
    ElgClosure fn;
};

/// An elg::ProposedSize: the size a parent offers during layout. An unspecified dimension asks
/// for the ideal size, 0 for the minimum, and infinity for the maximum.
struct ElgProposedSize {
    bool has_width;  ///< false: the width is unspecified.
    float width;     ///< The proposed width in points, if `has_width`.
    bool has_height; ///< false: the height is unspecified.
    float height;    ///< The proposed height in points, if `has_height`.
};

/// An optional length (elg::OptionalLength, std::optional<float>).
struct ElgOptLen {
    bool has;    ///< false: none.
    float value; ///< The value, if `has`.
};

/// An elg::Alignment.
struct ElgAlignment {
    int32_t horizontal; ///< An elg::HorizontalAlignment: 0 leading, 1 center, 2 trailing.
    int32_t vertical;   ///< An elg::VerticalAlignment: 0 top, 1 center, 2 bottom.
};

/// An elg::KeyEvent.
struct ElgKeyEvent {
    int32_t kind;       ///< KeyEvent::Kind: 0 down, 1 up.
    int32_t key;        ///< An elg::Key: the key by meaning (layout-mapped), not its position.
    uint32_t modifiers; ///< elg::KeyModifiers bits: 1 shift, 2 control, 4 alt, 8 super (Cmd, the Windows key).
    bool repeat;        ///< Auto-repeat while held.
};

/// An elg::PointerEvent.
struct ElgPointerEvent {
    int32_t kind;             ///< PointerEvent::Kind: 0 down, 1 up, 2 move, 3 cancel, 4 hover.
    /// In points: window coordinates for an event handler, the view's own for View::on_pointer.
    ElgPoint location;
    int32_t button;           ///< An elg::MouseButton: 0 left, 1 middle, 2 right, 3 other.
    int32_t click_count;      ///< 2 for the second click of a double click, ...
    uint32_t modifiers;       ///< elg::KeyModifiers held when the button went down or up (see ElgKeyEvent).
    ElgPoint window_location; ///< Always in the window's coordinates.
};

/// An elg::ScrollEvent: a mouse wheel or trackpad scroll. Deltas are in points, positive toward
/// the end of the content (wheel down: positive dy).
struct ElgScrollEvent {
    /// Where the pointer is: window coordinates for an event handler, the view's own for
    /// View::on_scroll.
    ElgPoint location;
    ElgPoint window_location; ///< Always in the window's coordinates.
    float dx, dy;             ///< How far it scrolled.
    bool precise;             ///< Trackpad-style fine deltas (apply directly) rather than wheel notches.
    uint32_t modifiers;       ///< elg::KeyModifiers held while scrolling (see ElgKeyEvent).
    int32_t phase;            ///< An elg::GesturePhase: 0 none, 1 began, 2 changed, 3 ended, 4 cancelled.
    bool momentum;            ///< The system's momentum after a flick, as the scroll coasts to a stop.
    double time;              ///< When it happened, in seconds on the event source's clock; 0 if unknown.
};

/// An elg::ClickEvent: a click on a view (View::on_click), pressed and released inside it.
struct ElgClickEvent {
    ElgPoint location;        ///< Where it was released, in the view's own coordinates.
    ElgPoint window_location; ///< The same, in the window's.
    int32_t button;           ///< An elg::MouseButton (see ElgPointerEvent).
    uint32_t modifiers;       ///< elg::KeyModifiers held when it was released (see ElgKeyEvent).
    int32_t click_count;      ///< 2 for a double click, ...
};

/// An elg::DragEvent: a drag in progress. Locations are in the view's own coordinates; the
/// translation is measured in the window.
struct ElgDragEvent {
    ElgPoint location;       ///< Where the pointer is now.
    ElgPoint start_location; ///< Where the press went down.
    ElgPoint translation;    ///< How far the pointer has moved since the press.
    ElgPoint velocity;       ///< Points per second, over the last moments of the drag.
    int32_t button;          ///< An elg::MouseButton (see ElgPointerEvent).
    uint32_t modifiers;      ///< elg::KeyModifiers held when the press went down (see ElgKeyEvent).
};

/// An elg::MagnifyEvent: pinching on a trackpad (or a touch screen).
struct ElgMagnifyEvent {
    ElgPoint location; ///< Where the gesture is.
    float scale;       ///< Since the gesture began: 2 is twice as big.
    float delta;       ///< Since the last event, as a factor.
    int32_t phase;     ///< An elg::GesturePhase (see ElgScrollEvent).
};

/// An elg::RotateEvent: turning two fingers on a trackpad.
struct ElgRotateEvent {
    ElgPoint location; ///< Where the gesture is.
    float degrees;     ///< Since the gesture began, clockwise.
    float delta;       ///< Since the last event, in degrees.
    int32_t phase;     ///< An elg::GesturePhase (see ElgScrollEvent).
};

/// An elg::TextInputEvent: text from the keyboard or an input method.
struct ElgTextInputEvent {
    /// TextInputEvent::Kind: 0 commit (insert `text`), 1 composition (an IME's in-progress text,
    /// replacing the previous one; empty ends it).
    int32_t kind;
    ElgStr text;   ///< The text, valid during the call.
    size_t cursor; ///< Composition: the IME's caret, in bytes into `text`.
};

/// An elg::EventResult: what an event handler did with an event.
struct ElgEventResult {
    /// Stops the event from reaching outer handlers. Consuming a down captures the pointer: the
    /// handler then receives the moves and the up, wherever the pointer goes.
    bool consumed;
    bool redraw; ///< The handler changed how something looks; the window needs a new frame.
    /// With `consumed` on a down: gesture recognizers around the view may take the press over, and
    /// the handler then gets a cancel. For presses that are clicks; handlers that track the pointer
    /// themselves leave it false.
    bool cancellable;
};

/// elg::FloatingPlacement: edge (FloatingEdge), alignment (FloatingAlignment), and an optional
/// position instead of an edge.
struct ElgFloatingPlacement {
    /// The anchor's side the content opens on, flipped when there isn't room: 0 bottom, 1 top,
    /// 2 trailing, 3 leading.
    int32_t edge;
    int32_t alignment; ///< Across that edge, lined up with the anchor's: 0 start, 1 center, 2 end.
    float spacing;     ///< Between the anchor and the content, in points (negative: overlapping).
    ElgPoint offset;   ///< Then moved by this, in points.
    bool has_position; ///< Use `position` instead of `edge`.
    ElgPoint position; ///< The content's top-leading corner, in the anchor's coordinates.
};

/// An optional elg::PopupMotion: `has` false is nullopt (the style's); null transition or
/// animation is PopupMotion::none(). Borrowed.
struct ElgPopupMotion {
    bool has;                          ///< false: nullopt.
    const elg::Transition* transition; ///< How the content enters and leaves.
    const elg::Animation* animation;   ///< The transition's timing.
};

/// An elg::HoverDelay: how long the pointer must rest on a view before it counts as hovered, and
/// how long after it leaves before it stops counting.
struct ElgHoverDelay {
    double enter; ///< Seconds before it's hovered.
    double exit;  ///< Seconds before it's no longer hovered.
};

/// An elg::Shadow.
struct ElgShadow {
    ElgColor color;     ///< The shadow's color.
    float radius, x, y; ///< The blur radius and the offset, in points.
    float spread;       ///< Grows (negative: shrinks) a single shape before blurring, in points.
};

/// An elg::StrokeStyle.
struct ElgStrokeStyle {
    float width;       ///< Line width in points.
    int32_t cap;       ///< An elg::LineCap: 0 butt, 1 round, 2 square.
    int32_t join;      ///< An elg::LineJoin: 0 miter, 1 round, 2 bevel.
    float miter_limit; ///< Miter joins longer than this many widths become bevels.
    const float* dash; ///< On, off, on, off... lengths, read during the call; null: solid.
    size_t dash_count; ///< Number of `dash` lengths.
    float dash_phase;  ///< How far into the dash pattern the stroke starts.
};

/// An elg::RunStyle: how a run of text looks. Unset parts come from the environment.
struct ElgRunStyle {
    bool has_weight;     ///< false: the font's weight.
    int32_t weight;      ///< An elg::Weight, if `has_weight`.
    bool italic;         ///< The italic face.
    ElgStr family;       ///< A registered family (e.g. monospace for code); empty: the font's.
    bool has_color;      ///< false: the environment's foreground style.
    ElgColor color;      ///< The text color, if `has_color`.
    bool has_background; ///< false: no highlight.
    ElgColor background; ///< A rounded highlight behind the run (mentions, inline code), if `has_background`.
    bool underline;      ///< Underlined.
    bool strikethrough;  ///< Struck through.
    bool keep_together;  ///< Never broken across lines (implied by a background).
};

/// An elg::ImageSource: an icon, an image file, or image data. Set exactly one of the first three
/// and leave the others empty.
struct ElgImageSource {
    ElgStr system_name; ///< An icon, named as for elg::Image(): "set.icon-name.variant", set and variant optional.
    ElgStr file;        ///< A raster image file's path (PNG, JPEG, ...).
    ElgBytes data;      ///< Encoded image data in memory, in any of those formats (copied).
    float scale;        ///< Raster images: pixels per point, e.g. 2 for double-resolution artwork.
};

} // extern "C"

namespace elg_shim {

// Thrown through Elegance when a Rust closure panicked; the panic payload stays on the Rust side.
struct RustPanic final : std::exception {
    [[nodiscard]] const char* what() const noexcept override { return "a Rust callback panicked"; }
};

// Owns a Rust closure.
class RustFn {
public:
    explicit RustFn(ElgClosure closure) noexcept : closure_(closure) {}
    ~RustFn() {
        if (closure_.drop) {
            closure_.drop(closure_.data);
        }
    }
    RustFn(const RustFn&) = delete;
    RustFn& operator=(const RustFn&) = delete;
    RustFn(RustFn&&) = delete;
    RustFn& operator=(RustFn&&) = delete;

    void call(void* args, void* ret) const {
        if (!closure_.call(closure_.data, args, ret)) {
            throw RustPanic();
        }
    }
    [[nodiscard]] void* data() const noexcept { return closure_.data; }

private:
    ElgClosure closure_;
};

using SharedFn = std::shared_ptr<const RustFn>;

inline SharedFn share(ElgClosure closure) {
    if (!closure.call) {
        if (closure.drop) {
            closure.drop(closure.data);
        }
        return nullptr;
    }
    return std::make_shared<const RustFn>(closure);
}

// Calls a borrowed closure (one that lives for the duration of a call).
inline void call_now(const ElgClosure& closure, void* args, void* ret) {
    if (!closure.call(closure.data, args, ret)) {
        throw RustPanic();
    }
}

inline std::string str(ElgStr s) {
    return s.ptr ? std::string(s.ptr, s.len) : std::string();
}

inline std::string_view view(ElgStr s) {
    return s.ptr ? std::string_view(s.ptr, s.len) : std::string_view();
}

inline ElgStr c_str(const std::string& s) {
    return {s.data(), s.size()};
}

inline std::filesystem::path path(ElgStr s) {
    const std::string_view text = view(s);
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

inline std::string utf8(const std::filesystem::path& p) {
    const std::u8string text = p.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

inline std::span<const unsigned char> bytes(ElgBytes b) {
    return b.ptr ? std::span<const unsigned char>(b.ptr, b.len) : std::span<const unsigned char>();
}

inline void sink(ElgStrSink out, void* ctx, const std::string& s) {
    out(ctx, s.data(), s.size());
}

// --- std::function adapters over Rust closures ---

inline std::function<void()> action(ElgClosure closure) {
    const SharedFn f = share(closure);
    if (!f) {
        return {};
    }
    return [f] { f->call(nullptr, nullptr); };
}

// A closure returning R, written by the Rust side into a default-constructed R.
template <class R>
std::function<R()> producer(ElgClosure closure) {
    const SharedFn f = share(closure);
    if (!f) {
        return {};
    }
    return [f] {
        R result{};
        f->call(nullptr, &result);
        return result;
    };
}

// A closure taking one argument; Rust receives a pointer to `A` (a C type).
template <class A>
std::function<void(A)> consumer(ElgClosure closure) {
    const SharedFn f = share(closure);
    if (!f) {
        return {};
    }
    return [f](A value) { f->call(&value, nullptr); };
}

inline std::function<elg::View()> view_builder(ElgClosure closure) {
    return producer<elg::View>(closure);
}

// --- Conversions between C structs and Elegance values ---

inline elg::Color color(ElgColor c) {
    return {c.r, c.g, c.b, c.a};
}

inline ElgColor c_color(elg::Color c) {
    return {c.r, c.g, c.b, c.a};
}

inline elg::Size size(ElgSize s) {
    return {s.width, s.height};
}

inline elg::Point point(ElgPoint p) {
    return {p.x, p.y};
}

inline elg::Rect rect(ElgRect r) {
    return {r.x, r.y, r.width, r.height};
}

inline ElgRect c_rect(elg::Rect r) {
    return {r.x, r.y, r.width, r.height};
}

inline elg::ProposedSize proposed(ElgProposedSize p) {
    elg::ProposedSize result;
    if (p.has_width) {
        result.width = p.width;
    }
    if (p.has_height) {
        result.height = p.height;
    }
    return result;
}

inline ElgProposedSize c_proposed(elg::ProposedSize p) {
    return {p.width.has_value(), p.width.value_or(0), p.height.has_value(), p.height.value_or(0)};
}

inline elg::OptionalLength length(ElgOptLen l) {
    return l.has ? elg::OptionalLength(l.value) : elg::OptionalLength();
}

inline elg::Alignment alignment(ElgAlignment a) {
    return {static_cast<elg::HorizontalAlignment>(a.horizontal), static_cast<elg::VerticalAlignment>(a.vertical)};
}

inline ElgAlignment c_alignment(elg::Alignment a) {
    return {static_cast<int32_t>(a.horizontal), static_cast<int32_t>(a.vertical)};
}

inline elg::Font font(const ElgFont& f) {
    const auto weight = static_cast<elg::Weight>(f.weight);
    const elg::Font result = f.family.len ? elg::Font::custom(str(f.family), f.size, weight) : elg::Font::system(f.size, weight);
    return result.italic(f.italic);
}

inline std::vector<elg::GradientStop> stops(const ElgShapeStyle& s) {
    std::vector<elg::GradientStop> result;
    result.reserve(s.stop_count);
    for (size_t i = 0; i < s.stop_count; ++i) {
        result.push_back({color(s.stops[i].color), s.stops[i].location});
    }
    return result;
}

inline elg::ShapeStyle shape_style(const ElgShapeStyle& s) {
    switch (s.kind) {
    case 1: return elg::LinearGradient(stops(s), elg::UnitPoint{s.x0, s.y0}, elg::UnitPoint{s.x1, s.y1});
    case 2: return elg::RadialGradient(stops(s), elg::UnitPoint{s.x0, s.y0}, s.r0, s.r1);
    case 3: return elg::EllipticalGradient(stops(s), elg::UnitPoint{s.x0, s.y0}, s.r0, s.r1);
    case 4: return elg::AngularGradient(stops(s), elg::UnitPoint{s.x0, s.y0}, s.r0, s.r1);
    default: return color(s.color);
    }
}

inline elg::EdgeInsets edge_insets(ElgEdgeInsets i) {
    return {i.top, i.leading, i.bottom, i.trailing};
}

inline elg::FloatingPlacement placement(const ElgFloatingPlacement& p) {
    elg::FloatingPlacement result;
    result.edge = static_cast<elg::FloatingEdge>(p.edge);
    result.alignment = static_cast<elg::FloatingAlignment>(p.alignment);
    result.spacing = p.spacing;
    result.offset = point(p.offset);
    if (p.has_position) {
        result.position = point(p.position);
    }
    return result;
}

inline std::optional<elg::PopupMotion> motion(const ElgPopupMotion& m) {
    if (!m.has) {
        return std::nullopt;
    }
    if (!m.transition || !m.animation) {
        return elg::PopupMotion::none();
    }
    return elg::PopupMotion(*m.transition, *m.animation);
}

inline std::optional<elg::Animation> animation(const elg::Animation* a) {
    return a ? std::optional<elg::Animation>(*a) : std::nullopt;
}

inline elg::UnitPoint unit_point(ElgPoint p) {
    return {p.x, p.y};
}

inline elg::HoverDelay hover_delay(ElgHoverDelay d) {
    return {d.enter, d.exit};
}

inline elg::Shadow shadow(ElgShadow s) {
    return {color(s.color), s.radius, s.x, s.y, s.spread};
}

inline elg::StrokeStyle stroke_style(const ElgStrokeStyle& s) {
    elg::StrokeStyle result;
    result.width = s.width;
    result.cap = static_cast<elg::LineCap>(s.cap);
    result.join = static_cast<elg::LineJoin>(s.join);
    result.miter_limit = s.miter_limit;
    if (s.dash) {
        result.dash.assign(s.dash, s.dash + s.dash_count);
    }
    result.dash_phase = s.dash_phase;
    return result;
}

inline elg::RunStyle run_style(const ElgRunStyle& s) {
    elg::RunStyle result;
    if (s.has_weight) {
        result.weight = static_cast<elg::Weight>(s.weight);
    }
    result.italic = s.italic;
    result.family = str(s.family);
    if (s.has_color) {
        result.color = color(s.color);
    }
    if (s.has_background) {
        result.background = color(s.background);
    }
    result.underline = s.underline;
    result.strikethrough = s.strikethrough;
    result.keep_together = s.keep_together;
    return result;
}

inline elg::ImageSource image_source(const ElgImageSource& s) {
    elg::ImageSource result;
    result.system_name = str(s.system_name);
    result.file = path(s.file);
    result.data = bytes(s.data);
    result.scale = s.scale;
    return result;
}

inline ElgKeyEvent c_key_event(const elg::KeyEvent& e) {
    return {static_cast<int32_t>(e.kind), static_cast<int32_t>(e.key), static_cast<uint32_t>(e.modifiers), e.repeat};
}

inline ElgPoint c_point(elg::Point p) {
    return {p.x, p.y};
}

inline ElgPointerEvent c_pointer_event(const elg::PointerEvent& e) {
    return {static_cast<int32_t>(e.kind), c_point(e.location), static_cast<int32_t>(e.button), e.click_count,
            static_cast<uint32_t>(e.modifiers), c_point(e.window_location)};
}

inline ElgScrollEvent c_scroll_event(const elg::ScrollEvent& e) {
    return {c_point(e.location), c_point(e.window_location), e.dx, e.dy, e.precise, static_cast<uint32_t>(e.modifiers),
            static_cast<int32_t>(e.phase), e.momentum, e.time};
}

inline ElgClickEvent c_click_event(const elg::ClickEvent& e) {
    return {c_point(e.location), c_point(e.window_location), static_cast<int32_t>(e.button),
            static_cast<uint32_t>(e.modifiers), e.click_count};
}

inline ElgDragEvent c_drag_event(const elg::DragEvent& e) {
    return {c_point(e.location), c_point(e.start_location), c_point(e.translation), c_point(e.velocity),
            static_cast<int32_t>(e.button), static_cast<uint32_t>(e.modifiers)};
}

inline ElgMagnifyEvent c_magnify_event(const elg::MagnifyEvent& e) {
    return {{e.location.x, e.location.y}, e.scale, e.delta, static_cast<int32_t>(e.phase)};
}

inline ElgRotateEvent c_rotate_event(const elg::RotateEvent& e) {
    return {{e.location.x, e.location.y}, e.degrees, e.delta, static_cast<int32_t>(e.phase)};
}

// The C form of each prop value type, for kind 0 (static) props.
template <class T>
struct PropValue;

template <>
struct PropValue<float> {
    static float from(const void* v) { return *static_cast<const float*>(v); }
};
template <>
struct PropValue<bool> {
    static bool from(const void* v) { return *static_cast<const bool*>(v); }
};
template <>
struct PropValue<int> {
    static int from(const void* v) { return *static_cast<const int32_t*>(v); }
};
template <>
struct PropValue<double> {
    static double from(const void* v) { return *static_cast<const double*>(v); }
};
template <>
struct PropValue<elg::Weight> {
    static elg::Weight from(const void* v) { return static_cast<elg::Weight>(*static_cast<const int32_t*>(v)); }
};
template <>
struct PropValue<elg::EdgeInsets> {
    static elg::EdgeInsets from(const void* v) { return edge_insets(*static_cast<const ElgEdgeInsets*>(v)); }
};
template <>
struct PropValue<std::optional<float>> {
    static std::optional<float> from(const void* v) {
        const auto& o = *static_cast<const ElgOptLen*>(v);
        return o.has ? std::optional<float>(o.value) : std::nullopt;
    }
};
template <>
struct PropValue<std::string> {
    static std::string from(const void* v) { return str(*static_cast<const ElgStr*>(v)); }
};
template <>
struct PropValue<elg::Color> {
    static elg::Color from(const void* v) { return color(*static_cast<const ElgColor*>(v)); }
};
template <>
struct PropValue<elg::Size> {
    static elg::Size from(const void* v) { return size(*static_cast<const ElgSize*>(v)); }
};
template <>
struct PropValue<elg::Font> {
    static elg::Font from(const void* v) { return font(*static_cast<const ElgFont*>(v)); }
};
template <>
struct PropValue<elg::ShapeStyle> {
    static elg::ShapeStyle from(const void* v) { return shape_style(*static_cast<const ElgShapeStyle*>(v)); }
};
template <>
struct PropValue<elg::ColorScheme> {
    static elg::ColorScheme from(const void* v) { return static_cast<elg::ColorScheme>(*static_cast<const int32_t*>(v)); }
};
template <>
struct PropValue<elg::PointerStyle> {
    static elg::PointerStyle from(const void* v) { return static_cast<elg::PointerStyle>(*static_cast<const int32_t*>(v)); }
};
template <>
struct PropValue<elg::Theme> {
    static elg::Theme from(const void* v) { return *static_cast<const elg::Theme*>(v); }
};
template <>
struct PropValue<elg::HoverDelay> {
    static elg::HoverDelay from(const void* v) { return hover_delay(*static_cast<const ElgHoverDelay*>(v)); }
};

// ShapeStyle has no default constructor; reactive closures write into one made from a color.
template <class T>
T blank() {
    if constexpr (std::is_same_v<T, elg::ShapeStyle>) {
        return elg::ShapeStyle(elg::Color{});
    } else if constexpr (std::is_same_v<T, elg::Font>) {
        return elg::Font::body;
    } else {
        return T{};
    }
}

template <class T>
elg::Prop<T> prop(const ElgProp& p) {
    switch (p.kind) {
    case 1: {
        const SharedFn f = share(p.fn);
        return elg::Prop<T>([f] {
            T result = blank<T>();
            f->call(nullptr, &result);
            return result;
        });
    }
    case 2: {
        const SharedFn f = share(p.fn);
        return elg::Prop<T>([f](const elg::EnvironmentValues& environment) {
            T result = blank<T>();
            f->call(const_cast<elg::EnvironmentValues*>(&environment), &result);
            return result;
        });
    }
    case 3:
        if constexpr (std::is_convertible_v<elg::Color, T>) {
            return elg::Prop<T>(static_cast<elg::ThemeColor>(p.theme));
        } else {
            return elg::Prop<T>(blank<T>());
        }
    default: return elg::Prop<T>(PropValue<T>::from(p.value));
    }
}

// --- Views across the boundary ---

inline elg::View* out(elg::View view) {
    return new elg::View(std::move(view));
}

inline elg::View take(elg::View* view) {
    elg::View result = std::move(*view);
    delete view;
    return result;
}

inline std::vector<elg::View> take_all(elg::View** views, size_t count) {
    std::vector<elg::View> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        result.push_back(take(views[i]));
    }
    return result;
}

// A Rust component: its body runs each time the view is mounted.
struct RustComponent {
    SharedFn body_fn;
    [[nodiscard]] elg::View body() const {
        elg::View result;
        body_fn->call(nullptr, &result);
        return result;
    }
};

// --- Errors ---

// Records the current exception for elg_take_error().
void capture_error() noexcept;

// --- ABI layout ---

// One line, "name size align", of an elg_layouts_* function: elegance-sys's layout test compares
// each C struct with its Rust declaration.
inline void layout(ElgStrSink out, void* ctx, const char* name, size_t size, size_t align) {
    sink(out, ctx, std::string(name) + ' ' + std::to_string(size) + ' ' + std::to_string(align) + '\n');
}

} // namespace elg_shim

#define ELG_LAYOUT(T) ::elg_shim::layout(out, ctx, #T, sizeof(T), alignof(T))

#define ELG_TRY try {
#define ELG_CATCH(fallback)               \
    }                                     \
    catch (...) {                         \
        ::elg_shim::capture_error();      \
        return fallback;                  \
    }
#define ELG_CATCH_VOID                    \
    }                                     \
    catch (...) {                         \
        ::elg_shim::capture_error();      \
    }

#endif // ELG_RUST_SHIM_H
