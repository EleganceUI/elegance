// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Extension points implemented in Rust: layouts, modifiers, event handlers. (Styles: styles.cpp.)

#include "shim.h"

#include <exception>

using namespace elg;
using namespace elg_shim;

// --- Layout ---

/// What a Rust layout's closure (elg_rust_layout_view()) is asked to do: one of elg::Layout's
/// methods.
struct ElgLayoutCall {
    /// 0: size_that_fits (ret ElgSize*), 1: place_children, 2: spacer_axis (ret int32_t*: -1 none,
    /// else an elg::Axis, 0 horizontal or 1 vertical).
    int32_t op;
    ElgProposedSize proposal;    ///< Ops 0 and 1: the size the parent proposes.
    ElgRect bounds;              ///< Op 1: where the children go, with the size reported for `proposal`.
    const LayoutChild* children; ///< Ops 0 and 1: the children, valid during the call.
    size_t count;                ///< Number of `children`.
    /// Ops 0 and 1: the view's cache storage, for the elg_layout_cache_* functions, valid during the
    /// call; null: uncached.
    LayoutCache* cache;
};

namespace {

class RustLayout final : public Layout {
public:
    explicit RustLayout(SharedFn f) : f_(std::move(f)) {}

    [[nodiscard]] Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override {
        return size(proposal, {}, children, nullptr);
    }
    void place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children) const override {
        place(bounds, proposal, children, nullptr);
    }
    [[nodiscard]] std::optional<Axis> spacer_axis() const override {
        ElgLayoutCall call{2, {}, {}, nullptr, 0, nullptr};
        int32_t axis = -1;
        f_->call(&call, &axis);
        if (axis < 0) {
            return std::nullopt;
        }
        return static_cast<Axis>(axis);
    }
    Size cached_size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children, LayoutCache& cache) const override {
        return size(proposal, {}, children, &cache);
    }
    void cached_place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children,
                               LayoutCache& cache) const override {
        place(bounds, proposal, children, &cache);
    }

private:
    Size size(ProposedSize proposal, Rect, std::span<const LayoutChild> children, LayoutCache* cache) const {
        ElgLayoutCall call{0, c_proposed(proposal), {}, children.data(), children.size(), cache};
        ElgSize result{};
        f_->call(&call, &result);
        return {result.width, result.height};
    }
    void place(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children, LayoutCache* cache) const {
        ElgLayoutCall call{1, c_proposed(proposal), c_rect(bounds), children.data(), children.size(), cache};
        f_->call(&call, nullptr);
    }

    SharedFn f_;
};

} // namespace

/// A container laid out by Rust: an elg::Layout whose methods call `layout`, used like a stack.
///
/// @param layout Consumed closure; args: `ElgLayoutCall*`, ret: what its `op` says. Required: none
///   is a usage error.
/// @param children The child views: each is consumed; the array is borrowed.
/// @param count Number of `children`.
/// @return The container, owned by the caller, or nullptr on failure.
ELG_EXPORT View* elg_rust_layout_view(ElgClosure layout, View** children, size_t count) {
    ELG_TRY
    ViewArray views{children, count};
    const Consumed consumed(layout, views);
    const RustLayout rust_layout(required(layout, "layout"));
    return out(rust_layout(take_all(views)));
    ELG_CATCH(nullptr)
}

/// The Rust cache stored in `cache`, or null.
///
/// @param cache An ElgLayoutCall's `cache` (borrowed).
/// @return The `data` of the closure elg_layout_cache_set() stored, owned by the cache; null if
///   none is stored.
ELG_EXPORT void* elg_layout_cache_get(const LayoutCache* cache) {
    if (!cache->value) {
        return nullptr;
    }
    return static_cast<const RustFn*>(cache->value.get())->data();
}
/// Stores a Rust cache in `cache` (LayoutCache::value), replacing (and dropping) the one before.
///
/// @param cache An ElgLayoutCall's `cache` (borrowed).
/// @param owner Consumed closure that's never called: its `data` is the cache, and its `drop` runs
///   when it's replaced or the view goes away.
ELG_EXPORT void elg_layout_cache_set(LayoutCache* cache, ElgClosure owner) {
    ELG_TRY
    const Consumed consumed(owner);
    cache->value = std::const_pointer_cast<RustFn>(hold(owner));
    ELG_CATCH_VOID
}
/// Reads LayoutCache::stale: set when the children change or one of them may have a new size.
///
/// @param cache An ElgLayoutCall's `cache` (borrowed).
/// @return true if the cache needs refreshing.
ELG_EXPORT bool elg_layout_cache_stale(const LayoutCache* cache) {
    return cache->stale;
}
/// Sets LayoutCache::stale; clear it once the cache is refreshed.
///
/// @param cache An ElgLayoutCall's `cache` (borrowed).
/// @param stale Whether the cache needs refreshing.
ELG_EXPORT void elg_layout_cache_set_stale(LayoutCache* cache, bool stale) {
    cache->stale = stale;
}

/// Wraps `LayoutChild::size_that_fits()`: measures a child for a proposal.
///
/// @param child One of an ElgLayoutCall's `children`, or an ElgLayoutModifierCall's `content`
///   (borrowed).
/// @param proposal The proposed size.
/// @return The child's size in points, or zero on failure.
ELG_EXPORT ElgSize elg_layout_child_size_that_fits(const LayoutChild* child, ElgProposedSize proposal) {
    ELG_TRY
    const Size s = child->size_that_fits(proposed(proposal));
    return {s.width, s.height};
    ELG_CATCH(ElgSize{})
}
/// Wraps `LayoutChild::place()`: positions a child's top-left corner; it sizes itself from
/// `proposal`.
///
/// @param child One of an ElgLayoutCall's `children`, or an ElgLayoutModifierCall's `content`
///   (borrowed).
/// @param position Where its top-left corner goes, in points.
/// @param proposal The proposed size.
ELG_EXPORT void elg_layout_child_place(const LayoutChild* child, ElgPoint position, ElgProposedSize proposal) {
    ELG_TRY
    child->place(point(position), proposed(proposal));
    ELG_CATCH_VOID
}
/// Wraps `LayoutChild::layout_priority()`.
///
/// @param child A child (borrowed).
/// @return The child's layout priority.
ELG_EXPORT float elg_layout_child_priority(const LayoutChild* child) {
    return child->layout_priority();
}
/// Wraps `LayoutChild::is_spacer()`.
///
/// @param child A child (borrowed).
/// @return true if the child is a Spacer.
ELG_EXPORT bool elg_layout_child_is_spacer(const LayoutChild* child) {
    return child->is_spacer();
}

// --- Layout values set from Rust ---
//
// One C++ key carries every Rust key. Before a lookup the Rust side records which key it wants;
// each modifier's `matches` closure (ret: bool*) answers whether it holds that key.

namespace {

struct RustLayoutValue {
    const void* data = nullptr;
};

struct RustLayoutKey {
    using Value = RustLayoutValue;
    static RustLayoutValue default_value() { return {}; }
};

class RustLayoutValueModifier final : public LayoutValueModifierBase {
public:
    explicit RustLayoutValueModifier(SharedFn owner) : owner_(std::move(owner)), value_{owner_->data()} {}
    [[nodiscard]] const void* value_for(const std::type_info& key) const noexcept override {
        if (key != typeid(RustLayoutKey)) {
            return nullptr;
        }
        bool matches = false;
        try {
            owner_->call(nullptr, &matches);
        } catch (...) {
            // value_for can't throw, so the panic goes to the UI thread, which rethrows it: it
            // reaches App::run like a panic in any app callback.
            try {
                post_to_ui_thread([error = std::current_exception()] { std::rethrow_exception(error); });
            } catch (...) {
                // Swallowed: posting failed (out of memory), and value_for can't throw.
            }
            return nullptr;
        }
        return matches ? &value_ : nullptr;
    }

private:
    SharedFn owner_;
    RustLayoutValue value_;
};

} // namespace

/// Carries a Rust value for the parent layout without changing layout, like View::layout_value().
///
/// @param v The view to modify (borrowed).
/// @param owner Consumed closure: its `data` is the value, and its `call` (args: null, ret: `bool*`)
///   answers whether it holds the key the Rust side is looking up. A panic there counts as false
///   for the lookup, and propagates out of App::run like one in any callback. Required: none is a
///   usage error.
/// @return The modified view, owned by the caller, or nullptr on failure.
ELG_EXPORT View* elg_mod_rust_layout_value(const View* v, ElgClosure owner) {
    ELG_TRY
    const Consumed consumed(owner);
    return out(v->modifier(RustLayoutValueModifier(required(owner, "owner"))));
    ELG_CATCH(nullptr)
}

/// The data of the matching Rust layout value on `child`, or null: `LayoutChild::value()` for the
/// one C++ key that carries every Rust key.
///
/// @param child A child (borrowed).
/// @return The `data` of the elg_mod_rust_layout_value() closure that matched, owned by its view;
///   null if none did, or on failure.
ELG_EXPORT const void* elg_layout_child_rust_value(const LayoutChild* child) {
    ELG_TRY
    return child->value<RustLayoutKey>().data;
    ELG_CATCH(nullptr)
}

// --- Modifiers ---

namespace {

class RustViewModifier final : public ViewModifier {
public:
    explicit RustViewModifier(SharedFn f) : f_(std::move(f)) {}
    // args: View* (owned by the Rust side), ret: View*.
    [[nodiscard]] View body(View content) const override {
        View result;
        f_->call(new View(std::move(content)), &result);
        return result;
    }

private:
    SharedFn f_;
};

} // namespace

/// An elg::ViewModifier implemented in Rust: `body` composes the content with other views and
/// modifiers, once, when the view is mounted.
///
/// @param v The view to modify (borrowed).
/// @param body Consumed closure; args: `View*`, the content (owned by the Rust side, which must
///   consume or free it); ret: `View*` (set it with elg_view_move_into()). Required: none is a
///   usage error.
/// @return The modified view, owned by the caller, or nullptr on failure.
ELG_EXPORT View* elg_mod_rust_view_modifier(const View* v, ElgClosure body) {
    ELG_TRY
    const Consumed consumed(body);
    return out(v->modifier(RustViewModifier(required(body, "body"))));
    ELG_CATCH(nullptr)
}

/// What a Rust layout modifier's closure (elg_mod_rust_layout_modifier()) is asked to do: one of
/// elg::LayoutModifier's methods.
struct ElgLayoutModifierCall {
    int32_t op; ///< 0: size_that_fits (ret ElgSize*), 1: place, 2: layout_priority (ret ElgOptLen*)
    ElgProposedSize proposal;   ///< Ops 0 and 1: the size the parent proposes.
    ElgRect bounds;             ///< Op 1: where the view goes, with the size reported for `proposal`.
    const LayoutChild* content; ///< Ops 0 and 1: the wrapped content, valid during the call; op 2: null.
};

namespace {

class RustLayoutModifier final : public LayoutModifier {
public:
    explicit RustLayoutModifier(SharedFn f) : f_(std::move(f)) {}

    [[nodiscard]] Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const override {
        ElgLayoutModifierCall call{0, c_proposed(proposal), {}, &content};
        ElgSize result{};
        f_->call(&call, &result);
        return {result.width, result.height};
    }
    void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const override {
        ElgLayoutModifierCall call{1, c_proposed(proposal), c_rect(bounds), &content};
        f_->call(&call, nullptr);
    }
    [[nodiscard]] std::optional<float> layout_priority() const override {
        ElgLayoutModifierCall call{2, {}, {}, nullptr};
        ElgOptLen result{};
        f_->call(&call, &result);
        if (!result.has) {
            return std::nullopt;
        }
        return result.value;
    }

private:
    SharedFn f_;
};

class RustDecorationModifier final : public DecorationModifier {
public:
    RustDecorationModifier(SharedFn f, Placement placement, Alignment alignment)
        : f_(std::move(f)), placement_(placement), alignment_(alignment) {}
    [[nodiscard]] View decoration() const override {
        View result;
        f_->call(nullptr, &result);
        return result;
    }
    [[nodiscard]] Placement placement() const override { return placement_; }
    [[nodiscard]] Alignment alignment() const override { return alignment_; }

private:
    SharedFn f_;
    Placement placement_;
    Alignment alignment_;
};

class RustEnvironmentModifier final : public EnvironmentModifier {
public:
    explicit RustEnvironmentModifier(SharedFn f) : f_(std::move(f)) {}
    // args: EnvironmentValues*.
    void apply(EnvironmentValues& values) const override { f_->call(&values, nullptr); }

private:
    SharedFn f_;
};

class RustEffectModifier final : public EffectModifier {
public:
    explicit RustEffectModifier(SharedFn f) : f_(std::move(f)) {}
    // args: Effects*.
    void apply(Effects& effects) const override { f_->call(&effects, nullptr); }

private:
    SharedFn f_;
};

} // namespace

/// An elg::LayoutModifier implemented in Rust: it takes part in layout, wrapping the view.
///
/// @param v The view to modify (borrowed).
/// @param layout Consumed closure; args: `ElgLayoutModifierCall*`, ret: what its `op` says. For
///   layout_priority, `has` false keeps the content's own. Required: none is a usage error.
/// @return The modified view, owned by the caller, or nullptr on failure.
ELG_EXPORT View* elg_mod_rust_layout_modifier(const View* v, ElgClosure layout) {
    ELG_TRY
    const Consumed consumed(layout);
    return out(v->modifier(RustLayoutModifier(required(layout, "layout"))));
    ELG_CATCH(nullptr)
}
/// An elg::DecorationModifier implemented in Rust: a secondary view sized to the content, which
/// doesn't affect layout.
///
/// @param v The view to modify (borrowed).
/// @param decoration Consumed closure that builds the decoration; args: null, ret: `View*` (set it
///   with elg_view_move_into()). Required: none is a usage error.
/// @param placement DecorationModifier::Placement (behind, in_front, mask): 0 drawn behind the
///   content, 1 in front of it, 2 not drawn but masking the content with its alpha.
/// @param a How the decoration is aligned with the content.
/// @return The modified view, owned by the caller, or nullptr on failure.
ELG_EXPORT View* elg_mod_rust_decoration_modifier(const View* v, ElgClosure decoration, int32_t placement, ElgAlignment a) {
    ELG_TRY
    const Consumed consumed(decoration);
    return out(v->modifier(RustDecorationModifier(required(decoration, "decoration"),
                                                  static_cast<DecorationModifier::Placement>(placement), alignment(a))));
    ELG_CATCH(nullptr)
}
/// An elg::EnvironmentModifier implemented in Rust: changes inherited values for everything inside
/// the view.
///
/// @param v The view to modify (borrowed).
/// @param apply Consumed closure; args: `EnvironmentValues*`, to change with the elg_env_set_*
///   functions; ret: null. Required: none is a usage error.
/// @return The modified view, owned by the caller, or nullptr on failure.
ELG_EXPORT View* elg_mod_rust_environment_modifier(const View* v, ElgClosure apply) {
    ELG_TRY
    const Consumed consumed(apply);
    return out(v->modifier(RustEnvironmentModifier(required(apply, "apply"))));
    ELG_CATCH(nullptr)
}
/// An elg::EffectModifier implemented in Rust: changes how the view's content is drawn and hit
/// tested, not its layout.
///
/// @param v The view to modify (borrowed).
/// @param apply Consumed closure; args: `Effects*`, to change with the elg_effects_* functions;
///   ret: null. Required: none is a usage error.
/// @return The modified view, owned by the caller, or nullptr on failure.
ELG_EXPORT View* elg_mod_rust_effect_modifier(const View* v, ElgClosure apply) {
    ELG_TRY
    const Consumed consumed(apply);
    return out(v->modifier(RustEffectModifier(required(apply, "apply"))));
    ELG_CATCH(nullptr)
}

// Effects, for EffectModifier::apply. Each takes the `Effects*` an effect modifier's closure gets
// (borrowed).

/// Sets Effects::clip_shape: the content is clipped to `shape`, which fills the view's bounds.
///
/// @param e The effects (borrowed).
/// @param shape The shape (borrowed; copied); null: no clip.
ELG_EXPORT void elg_effects_set_clip_shape(Effects* e, const Shape* shape) {
    if (shape) {
        e->clip_shape = *shape;
    } else {
        e->clip_shape.reset();
    }
}
/// Sets Effects::opacity.
///
/// @param e The effects (borrowed).
/// @param opacity The opacity, 0 to 1: an ElgProp of float.
ELG_EXPORT void elg_effects_set_opacity(Effects* e, ElgProp opacity) {
    ELG_TRY
    const Consumed consumed(opacity);
    e->opacity = prop<float>(opacity);
    ELG_CATCH_VOID
}
/// Sets Effects::shadow, a drop shadow.
///
/// @param e The effects (borrowed).
/// @param s The shadow (borrowed); null: none.
ELG_EXPORT void elg_effects_set_shadow(Effects* e, const ElgShadow* s) {
    if (s) {
        e->shadow = shadow(*s);
    } else {
        e->shadow.reset();
    }
}
/// Sets Effects::offset_x and offset_y: moves the drawn content (and where it's hit) without
/// affecting layout.
///
/// @param e The effects (borrowed).
/// @param x Horizontal offset in points: an ElgProp of float.
/// @param y Vertical offset in points: an ElgProp of float.
ELG_EXPORT void elg_effects_set_offset(Effects* e, ElgProp x, ElgProp y) {
    ELG_TRY
    const Consumed consumed(x, y);
    e->offset_x = prop<float>(x);
    e->offset_y = prop<float>(y);
    ELG_CATCH_VOID
}
/// Sets Effects::z_index: the stacking order among siblings.
///
/// @param e The effects (borrowed).
/// @param z The z index: an ElgProp of float; null: unset.
ELG_EXPORT void elg_effects_set_z_index(Effects* e, const ElgProp* z) {
    ELG_TRY
    ElgProp value = z ? *z : ElgProp{};
    const Consumed consumed(value);
    if (z) {
        e->z_index = prop<float>(value);
    } else {
        e->z_index.reset();
    }
    ELG_CATCH_VOID
}
/// Sets Effects::content_shape: the view is hit tested on `shape` (filling its bounds) instead of
/// where its content draws.
///
/// @param e The effects (borrowed).
/// @param shape The shape (borrowed; copied); null: where the content draws.
ELG_EXPORT void elg_effects_set_content_shape(Effects* e, const Shape* shape) {
    if (shape) {
        e->content_shape = *shape;
    } else {
        e->content_shape.reset();
    }
}
/// Sets Effects::inner_shadow, a shadow inside the content's edges.
///
/// @param e The effects (borrowed).
/// @param s The shadow (borrowed); null: none.
ELG_EXPORT void elg_effects_set_inner_shadow(Effects* e, const ElgShadow* s) {
    if (s) {
        e->inner_shadow = shadow(*s);
    } else {
        e->inner_shadow.reset();
    }
}
/// Sets Effects::blur and blur_opaque.
///
/// @param e The effects (borrowed).
/// @param radius The blur radius in points: an ElgProp of float; null: no blur.
/// @param opaque Whether the content fills its bounds: its edges then stay sharp instead of fading
///   out.
ELG_EXPORT void elg_effects_set_blur(Effects* e, const ElgProp* radius, bool opaque) {
    ELG_TRY
    ElgProp value = radius ? *radius : ElgProp{};
    const Consumed consumed(value);
    if (radius) {
        e->blur = prop<float>(value);
    } else {
        e->blur.reset();
    }
    e->blur_opaque = opaque;
    ELG_CATCH_VOID
}
/// Sets Effects::color_filter: a color adjustment applied to the content's pixels, in sRGB with
/// straight alpha, as CSS filter functions are.
///
/// @param e The effects (borrowed).
/// @param kind A ColorFilter::Kind: 0 brightness, 1 contrast, 2 saturation, 3 grayscale,
///   4 hue_rotation, 5 sepia, 6 invert, 7 multiply, 8 luminance_to_alpha. kind < 0: none.
/// @param amount An ElgProp of float: degrees for hue_rotation; unused for multiply and
///   luminance_to_alpha. Ignored, and its closure dropped, when `kind` < 0.
/// @param c An ElgProp of elg::Color, for multiply only. Ignored, and its closure dropped, when
///   `kind` < 0.
ELG_EXPORT void elg_effects_set_color_filter(Effects* e, int32_t kind, ElgProp amount, ElgProp c) {
    ELG_TRY
    const Consumed consumed(amount, c);
    if (kind < 0) {
        e->color_filter.reset();
        return;
    }
    ColorFilter filter;
    filter.kind = static_cast<ColorFilter::Kind>(kind);
    filter.amount = prop<float>(amount);
    filter.color = prop<Color>(c);
    e->color_filter = std::move(filter);
    ELG_CATCH_VOID
}
/// Reads Effects::blend_mode.
///
/// @param e The effects (borrowed).
/// @return The elg::BlendMode (0 normal; see elg_effects_set_blend_mode()).
ELG_EXPORT int32_t elg_effects_blend_mode(const Effects* e) {
    return static_cast<int32_t>(e->blend_mode);
}
/// Sets Effects::blend_mode: how the view's pixels combine with what's behind them, as CSS's
/// mix-blend-mode.
///
/// @param e The effects (borrowed).
/// @param mode An elg::BlendMode: 0 normal, 1 multiply, 2 darken, 3 color_burn, 4 plus_darker,
///   5 screen, 6 lighten, 7 color_dodge, 8 plus_lighter, 9 overlay, 10 soft_light, 11 hard_light,
///   12 difference, 13 exclusion, 14 hue, 15 saturation, 16 color, 17 luminosity.
ELG_EXPORT void elg_effects_set_blend_mode(Effects* e, int32_t mode) {
    e->blend_mode = static_cast<BlendMode>(mode);
}
/// Reads Effects::compositing_group.
///
/// @param e The effects (borrowed).
/// @return true if the content is drawn offscreen even when nothing else needs it.
ELG_EXPORT bool elg_effects_compositing_group(const Effects* e) {
    return e->compositing_group;
}
/// Sets Effects::compositing_group.
///
/// @param e The effects (borrowed).
/// @param group Whether to draw the content offscreen even when nothing else needs it.
ELG_EXPORT void elg_effects_set_compositing_group(Effects* e, bool group) {
    e->compositing_group = group;
}
/// Sets Effects::rotation: the drawn content turns about the anchor (elg_effects_set_anchor()).
///
/// @param e The effects (borrowed).
/// @param degrees Clockwise, as an ElgProp of float; null: unset.
ELG_EXPORT void elg_effects_set_rotation(Effects* e, const ElgProp* degrees) {
    ELG_TRY
    ElgProp value = degrees ? *degrees : ElgProp{};
    const Consumed consumed(value);
    if (degrees) {
        e->rotation = prop<float>(value);
    } else {
        e->rotation.reset();
    }
    ELG_CATCH_VOID
}
/// Sets Effects::scale_x and scale_y: the drawn content scales about the anchor
/// (elg_effects_set_anchor()).
///
/// @param e The effects (borrowed).
/// @param x Horizontal scale (1: as is), as an ElgProp of float; null: unset.
/// @param y Vertical scale, as an ElgProp of float; null: unset.
ELG_EXPORT void elg_effects_set_scale(Effects* e, const ElgProp* x, const ElgProp* y) {
    ELG_TRY
    ElgProp horizontal = x ? *x : ElgProp{};
    ElgProp vertical = y ? *y : ElgProp{};
    const Consumed consumed(horizontal, vertical);
    if (x) {
        e->scale_x = prop<float>(horizontal);
    } else {
        e->scale_x.reset();
    }
    if (y) {
        e->scale_y = prop<float>(vertical);
    } else {
        e->scale_y.reset();
    }
    ELG_CATCH_VOID
}
/// Reads Effects::anchor.
///
/// @param e The effects (borrowed).
/// @return The unit point rotation and scale are about; (0.5, 0.5) is the middle.
ELG_EXPORT ElgPoint elg_effects_anchor(const Effects* e) {
    return {e->anchor.x, e->anchor.y};
}
/// Sets Effects::anchor: what rotation and scale are about.
///
/// @param e The effects (borrowed).
/// @param anchor A unit point of the view's bounds: (0, 0) top-leading, (1, 1) bottom-trailing.
ELG_EXPORT void elg_effects_set_anchor(Effects* e, ElgPoint anchor) {
    e->anchor = unit_point(anchor);
}
/// Reads Effects::allows_hit_testing.
///
/// @param e The effects (borrowed).
/// @return false if the view lets clicks through.
ELG_EXPORT bool elg_effects_allows_hit_testing(const Effects* e) {
    return e->allows_hit_testing;
}
/// Sets Effects::allows_hit_testing.
///
/// @param e The effects (borrowed).
/// @param enabled false: the view isn't hit, and clicks go to what's behind it.
ELG_EXPORT void elg_effects_set_allows_hit_testing(Effects* e, bool enabled) {
    e->allows_hit_testing = enabled;
}

// --- Event handlers ---

/// An event for a Rust event handler (elg_mod_rust_event_modifier()): a call of one of
/// elg::EventHandler's methods.
struct ElgEventCall {
    /// 0 pointer, 1 hover, 2 scroll, 3 key, 4 focusable (ret bool*), 5 focus, 6 accepts_text_input
    /// (ret bool*), 7 text_input, 8 hover_delay (ret ElgOptHoverDelay*), 9 covers_frame (ret bool*),
    /// 10 pointer_style (ret int32_t*), 11 magnify, 12 rotate, 13 two_finger_double_tap.
    /// The others: ret ElgEventResult*. `frame`: pointer and scroll.
    int32_t op;
    ElgPointerEvent pointer;   ///< Op 0: the event.
    ElgRect frame;             ///< Ops 0 and 2: the view's current frame, in window coordinates.
    bool flag;                 ///< hover: inside; focus: focused
    ElgScrollEvent scroll;     ///< Op 2: the event.
    ElgKeyEvent key;           ///< Op 3: the event.
    ElgTextInputEvent text;    ///< Op 7: the event; its text is valid during the call.
    ElgPoint location;         ///< pointer_style, two_finger_double_tap
    ElgMagnifyEvent magnify;   ///< Op 11: the event.
    ElgRotateEvent rotate;     ///< Op 12: the event.
};

/// An optional elg::HoverDelay, an event handler's own (ElgEventCall op 8).
struct ElgOptHoverDelay {
    bool has;            ///< false: the environment's delay.
    ElgHoverDelay delay; ///< The delay, if `has`.
};

namespace {

class RustEventHandler final : public EventHandler {
public:
    explicit RustEventHandler(SharedFn f) : f_(std::move(f)) {}

    EventResult pointer(const PointerEvent& event, Rect frame) override {
        ElgEventCall call{};
        call.op = 0;
        call.pointer = c_pointer_event(event);
        call.frame = c_rect(frame);
        return result(call);
    }
    EventResult hover(bool inside) override {
        ElgEventCall call{};
        call.op = 1;
        call.flag = inside;
        return result(call);
    }
    [[nodiscard]] PointerStyle pointer_style(Point location) const override {
        ElgEventCall call{};
        call.op = 10;
        call.location = {location.x, location.y};
        int32_t style = 0;
        f_->call(&call, &style);
        return static_cast<PointerStyle>(style);
    }
    EventResult scroll(const ScrollEvent& event, Rect frame) override {
        ElgEventCall call{};
        call.op = 2;
        call.scroll = c_scroll_event(event);
        call.frame = c_rect(frame);
        return result(call);
    }
    EventResult magnify(const MagnifyEvent& event) override {
        ElgEventCall call{};
        call.op = 11;
        call.magnify = c_magnify_event(event);
        return result(call);
    }
    EventResult rotate(const RotateEvent& event) override {
        ElgEventCall call{};
        call.op = 12;
        call.rotate = c_rotate_event(event);
        return result(call);
    }
    EventResult two_finger_double_tap(Point location) override {
        ElgEventCall call{};
        call.op = 13;
        call.location = {location.x, location.y};
        return result(call);
    }
    EventResult key(const KeyEvent& event) override {
        ElgEventCall call{};
        call.op = 3;
        call.key = c_key_event(event);
        return result(call);
    }
    [[nodiscard]] bool focusable() const override { return flag(4); }
    EventResult focus(bool focused) override {
        ElgEventCall call{};
        call.op = 5;
        call.flag = focused;
        return result(call);
    }
    [[nodiscard]] bool accepts_text_input() const override { return flag(6); }
    EventResult text_input(const TextInputEvent& event) override {
        ElgEventCall call{};
        call.op = 7;
        call.text = {static_cast<int32_t>(event.kind), c_str(event.text), event.cursor};
        return result(call);
    }
    [[nodiscard]] std::optional<HoverDelay> hover_delay() const override {
        ElgEventCall call{};
        call.op = 8;
        ElgOptHoverDelay delay{};
        f_->call(&call, &delay);
        if (!delay.has) {
            return std::nullopt;
        }
        return elg_shim::hover_delay(delay.delay);
    }
    [[nodiscard]] bool covers_frame() const override { return flag(9); }

private:
    EventResult result(ElgEventCall& call) {
        ElgEventResult r{};
        f_->call(&call, &r);
        return {r.consumed, r.redraw, r.cancellable};
    }
    [[nodiscard]] bool flag(int32_t op) const {
        ElgEventCall call{};
        call.op = op;
        bool value = false;
        f_->call(&call, &value);
        return value;
    }

    SharedFn f_;
};

class RustEventModifier final : public EventModifier {
public:
    explicit RustEventModifier(SharedFn f) : f_(std::move(f)) {}
    // ret: ElgClosure* (the new handler).
    [[nodiscard]] std::unique_ptr<EventHandler> make_handler() const override {
        ElgClosure handler{};
        const Consumed consumed(handler);
        f_->call(nullptr, &handler);
        return std::make_unique<RustEventHandler>(required(handler, "the handler make_handler returned"));
    }

private:
    SharedFn f_;
};

} // namespace

/// An elg::EventModifier implemented in Rust: handles input for the view, with one handler per
/// mounted view.
///
/// @param v The view to modify (borrowed).
/// @param make_handler Consumed closure, called when the view mounts; args: null, ret:
///   `ElgClosure*`, the new handler, which Elegance consumes. The handler's args:
///   `ElgEventCall*`; its ret: what the call's `op` says. Required: none is a usage error.
/// @return The modified view, owned by the caller, or nullptr on failure.
ELG_EXPORT View* elg_mod_rust_event_modifier(const View* v, ElgClosure make_handler) {
    ELG_TRY
    const Consumed consumed(make_handler);
    return out(v->modifier(RustEventModifier(required(make_handler, "make_handler"))));
    ELG_CATCH(nullptr)
}

// --- ABI layout ---

/// Describes the layout of this file's structs, for elegance-sys's layout test: one line per
/// struct, "name size align".
///
/// @param out Receives each line.
/// @param ctx Passed to `out`.
ELG_EXPORT void elg_layouts_ext(ElgStrSink out, void* ctx) {
    ELG_LAYOUT(ElgLayoutCall);
    ELG_LAYOUT(ElgLayoutModifierCall);
    ELG_LAYOUT(ElgEventCall);
    ELG_LAYOUT(ElgOptHoverDelay);
}
