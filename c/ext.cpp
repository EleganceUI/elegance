// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Extension points implemented in Rust: layouts, modifiers, event handlers. (Styles: styles.cpp.)

#include "shim.h"

#include <cstdint>
#include <exception>
#include <string_view>

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
    const ElgLayoutChild* children; ///< Ops 0 and 1: the children, valid during the call.
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
        ElgLayoutCall call{0, c_proposed(proposal), {}, c_children(children.data()), children.size(), cache};
        ElgSize result{};
        f_->call(&call, &result);
        return {result.width, result.height};
    }
    void place(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children, LayoutCache* cache) const {
        ElgLayoutCall call{1, c_proposed(proposal), c_rect(bounds), c_children(children.data()), children.size(), cache};
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
    ViewArray views{children, count};
    const Consumed consumed(layout, views);
    ELG_TRY
    const RustLayout rust_layout(required(layout, "layout"));
    return out(rust_layout(take_all(views)));
    ELG_CATCH(nullptr)
}

/// The Rust cache stored in `cache`, or null.
///
/// @param cache An ElgLayoutCall's `cache` (borrowed).
/// @return The `data` of the closure elg_layout_cache_set() stored, owned by the cache; null if
///   none is stored. Null on failure.
ELG_EXPORT void* elg_layout_cache_get(const LayoutCache* cache) {
    ELG_TRY
    if (!cache->value) {
        return nullptr;
    }
    return static_cast<const RustFn*>(cache->value.get())->data();
    ELG_CATCH(nullptr)
}
/// Stores a Rust cache in `cache` (LayoutCache::value), replacing (and dropping) the one before.
///
/// @param cache An ElgLayoutCall's `cache` (borrowed).
/// @param owner Consumed closure that's never called: its `data` is the cache, and its `drop` runs
///   when it's replaced or the view goes away.
ELG_EXPORT void elg_layout_cache_set(LayoutCache* cache, ElgClosure owner) {
    const Consumed consumed(owner);
    ELG_TRY
    cache->value = std::const_pointer_cast<RustFn>(hold(owner));
    ELG_CATCH_VOID
}
/// Reads LayoutCache::stale: set when the children change or one of them may have a new size.
///
/// @param cache An ElgLayoutCall's `cache` (borrowed).
/// @return true if the cache needs refreshing. False on failure.
ELG_EXPORT bool elg_layout_cache_stale(const LayoutCache* cache) {
    ELG_TRY
    return cache->stale;
    ELG_CATCH(false)
}
/// Sets LayoutCache::stale; clear it once the cache is refreshed.
///
/// @param cache An ElgLayoutCall's `cache` (borrowed).
/// @param stale Whether the cache needs refreshing.
ELG_EXPORT void elg_layout_cache_set_stale(LayoutCache* cache, bool stale) {
    ELG_TRY
    cache->stale = stale;
    ELG_CATCH_VOID
}

/// Wraps `LayoutChild::size_that_fits()`: measures a child for a proposal.
///
/// @param child One of an ElgLayoutCall's `children`, or an ElgLayoutModifierCall's `content`
///   (borrowed).
/// @param proposal The proposed size.
/// @return The child's size in points, or zero on failure.
ELG_EXPORT ElgSize elg_layout_child_size_that_fits(const ElgLayoutChild* child, ElgProposedSize proposal) {
    ELG_TRY
    const Size s = cpp_children(child)->size_that_fits(proposed(proposal));
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
ELG_EXPORT void elg_layout_child_place(const ElgLayoutChild* child, ElgPoint position, ElgProposedSize proposal) {
    ELG_TRY
    cpp_children(child)->place(point(position), proposed(proposal));
    ELG_CATCH_VOID
}
/// Wraps `LayoutChild::layout_priority()`.
///
/// @param child A child (borrowed).
/// @return The child's layout priority. 0 on failure.
ELG_EXPORT float elg_layout_child_priority(const ElgLayoutChild* child) {
    ELG_TRY
    return cpp_children(child)->layout_priority();
    ELG_CATCH(0)
}
/// Wraps `LayoutChild::is_spacer()`.
///
/// @param child A child (borrowed).
/// @return true if the child is a Spacer. False on failure.
ELG_EXPORT bool elg_layout_child_is_spacer(const ElgLayoutChild* child) {
    ELG_TRY
    return cpp_children(child)->is_spacer();
    ELG_CATCH(false)
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
    static constexpr std::string_view name = "elg.rust_layout_value";
    static RustLayoutValue default_value() { return {}; }
};

class RustLayoutValueModifier final : public LayoutValueModifierBase {
public:
    explicit RustLayoutValueModifier(SharedFn owner) : owner_(std::move(owner)), value_{owner_->data()} {}
    [[nodiscard]] const void* value_for(std::uint64_t key) const noexcept override {
        if (key != elg::detail::key_id(RustLayoutKey::name)) {
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
    const Consumed consumed(owner);
    ELG_TRY
    return out(v->modifier(RustLayoutValueModifier(required(owner, "owner"))));
    ELG_CATCH(nullptr)
}

/// The data of the matching Rust layout value on `child`, or null: `LayoutChild::value()` for the
/// one C++ key that carries every Rust key.
///
/// @param child A child (borrowed).
/// @return The `data` of the elg_mod_rust_layout_value() closure that matched, owned by its view;
///   null if none did, or on failure.
ELG_EXPORT const void* elg_layout_child_rust_value(const ElgLayoutChild* child) {
    ELG_TRY
    return cpp_children(child)->value<RustLayoutKey>().data;
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
    const Consumed consumed(body);
    ELG_TRY
    return out(v->modifier(RustViewModifier(required(body, "body"))));
    ELG_CATCH(nullptr)
}

/// What a Rust layout modifier's closure (elg_mod_rust_layout_modifier()) is asked to do: one of
/// elg::LayoutModifier's methods.
struct ElgLayoutModifierCall {
    int32_t op; ///< 0: size_that_fits (ret ElgSize*), 1: place, 2: layout_priority (ret ElgOptLen*)
    ElgProposedSize proposal;   ///< Ops 0 and 1: the size the parent proposes.
    ElgRect bounds;             ///< Op 1: where the view goes, with the size reported for `proposal`.
    const ElgLayoutChild* content; ///< Ops 0 and 1: the wrapped content, valid during the call; op 2: null.
};

namespace {

class RustLayoutModifier final : public LayoutModifier {
public:
    explicit RustLayoutModifier(SharedFn f) : f_(std::move(f)) {}

    [[nodiscard]] Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const override {
        ElgLayoutModifierCall call{0, c_proposed(proposal), {}, c_children(&content)};
        ElgSize result{};
        f_->call(&call, &result);
        return {result.width, result.height};
    }
    void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const override {
        ElgLayoutModifierCall call{1, c_proposed(proposal), c_rect(bounds), c_children(&content)};
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
    const Consumed consumed(layout);
    ELG_TRY
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
    const Consumed consumed(decoration);
    ELG_TRY
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
    const Consumed consumed(apply);
    ELG_TRY
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
    const Consumed consumed(apply);
    ELG_TRY
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
    ELG_TRY
    if (shape) {
        e->clip_shape = *shape;
    } else {
        e->clip_shape.reset();
    }
    ELG_CATCH_VOID
}
/// Sets Effects::opacity.
///
/// @param e The effects (borrowed).
/// @param opacity The opacity, 0 to 1: an ElgProp of float.
ELG_EXPORT void elg_effects_set_opacity(Effects* e, ElgProp opacity) {
    const Consumed consumed(opacity);
    ELG_TRY
    e->opacity = prop<float>(opacity);
    ELG_CATCH_VOID
}
/// Sets Effects::shadow, a drop shadow.
///
/// @param e The effects (borrowed).
/// @param s The shadow (borrowed); null: none.
ELG_EXPORT void elg_effects_set_shadow(Effects* e, const ElgShadow* s) {
    ELG_TRY
    if (s) {
        e->shadow = shadow(*s);
    } else {
        e->shadow.reset();
    }
    ELG_CATCH_VOID
}
/// Sets Effects::offset_x and offset_y: moves the drawn content (and where it's hit) without
/// affecting layout.
///
/// @param e The effects (borrowed).
/// @param x Horizontal offset in points: an ElgProp of float.
/// @param y Vertical offset in points: an ElgProp of float.
ELG_EXPORT void elg_effects_set_offset(Effects* e, ElgProp x, ElgProp y) {
    const Consumed consumed(x, y);
    ELG_TRY
    e->offset_x = prop<float>(x);
    e->offset_y = prop<float>(y);
    ELG_CATCH_VOID
}
/// Sets Effects::z_index: the stacking order among siblings.
///
/// @param e The effects (borrowed).
/// @param z The z index: an ElgProp of float; null: unset.
ELG_EXPORT void elg_effects_set_z_index(Effects* e, const ElgProp* z) {
    ElgProp value = z ? *z : ElgProp{};
    const Consumed consumed(value);
    ELG_TRY
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
    ELG_TRY
    if (shape) {
        e->content_shape = *shape;
    } else {
        e->content_shape.reset();
    }
    ELG_CATCH_VOID
}
/// Sets Effects::inner_shadow, a shadow inside the content's edges.
///
/// @param e The effects (borrowed).
/// @param s The shadow (borrowed); null: none.
ELG_EXPORT void elg_effects_set_inner_shadow(Effects* e, const ElgShadow* s) {
    ELG_TRY
    if (s) {
        e->inner_shadow = shadow(*s);
    } else {
        e->inner_shadow.reset();
    }
    ELG_CATCH_VOID
}
/// Sets Effects::blur and blur_opaque.
///
/// @param e The effects (borrowed).
/// @param radius The blur radius in points: an ElgProp of float; null: no blur.
/// @param opaque Whether the content fills its bounds: its edges then stay sharp instead of fading
///   out.
ELG_EXPORT void elg_effects_set_blur(Effects* e, const ElgProp* radius, bool opaque) {
    ElgProp value = radius ? *radius : ElgProp{};
    const Consumed consumed(value);
    ELG_TRY
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
    const Consumed consumed(amount, c);
    ELG_TRY
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
/// @return The elg::BlendMode (0 normal; see elg_effects_set_blend_mode()). 0 on failure.
ELG_EXPORT int32_t elg_effects_blend_mode(const Effects* e) {
    ELG_TRY
    return static_cast<int32_t>(e->blend_mode);
    ELG_CATCH(0)
}
/// Sets Effects::blend_mode: how the view's pixels combine with what's behind them, as CSS's
/// mix-blend-mode.
///
/// @param e The effects (borrowed).
/// @param mode An elg::BlendMode: 0 normal, 1 multiply, 2 darken, 3 color_burn, 4 plus_darker,
///   5 screen, 6 lighten, 7 color_dodge, 8 plus_lighter, 9 overlay, 10 soft_light, 11 hard_light,
///   12 difference, 13 exclusion, 14 hue, 15 saturation, 16 color, 17 luminosity.
ELG_EXPORT void elg_effects_set_blend_mode(Effects* e, int32_t mode) {
    ELG_TRY
    e->blend_mode = static_cast<BlendMode>(mode);
    ELG_CATCH_VOID
}
/// Reads Effects::compositing_group.
///
/// @param e The effects (borrowed).
/// @return true if the content is drawn offscreen even when nothing else needs it. False on failure.
ELG_EXPORT bool elg_effects_compositing_group(const Effects* e) {
    ELG_TRY
    return e->compositing_group;
    ELG_CATCH(false)
}
/// Sets Effects::compositing_group.
///
/// @param e The effects (borrowed).
/// @param group Whether to draw the content offscreen even when nothing else needs it.
ELG_EXPORT void elg_effects_set_compositing_group(Effects* e, bool group) {
    ELG_TRY
    e->compositing_group = group;
    ELG_CATCH_VOID
}
/// Sets Effects::rotation: the drawn content turns about the anchor (elg_effects_set_anchor()).
///
/// @param e The effects (borrowed).
/// @param degrees Clockwise, as an ElgProp of float; null: unset.
ELG_EXPORT void elg_effects_set_rotation(Effects* e, const ElgProp* degrees) {
    ElgProp value = degrees ? *degrees : ElgProp{};
    const Consumed consumed(value);
    ELG_TRY
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
    ElgProp horizontal = x ? *x : ElgProp{};
    ElgProp vertical = y ? *y : ElgProp{};
    const Consumed consumed(horizontal, vertical);
    ELG_TRY
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
/// @return The unit point rotation and scale are about; (0.5, 0.5) is the middle. Zero on failure.
ELG_EXPORT ElgPoint elg_effects_anchor(const Effects* e) {
    ELG_TRY
    return {e->anchor.x, e->anchor.y};
    ELG_CATCH({})
}
/// Sets Effects::anchor: what rotation and scale are about.
///
/// @param e The effects (borrowed).
/// @param anchor A unit point of the view's bounds: (0, 0) top-leading, (1, 1) bottom-trailing.
ELG_EXPORT void elg_effects_set_anchor(Effects* e, ElgPoint anchor) {
    ELG_TRY
    e->anchor = unit_point(anchor);
    ELG_CATCH_VOID
}
/// Reads Effects::allows_hit_testing.
///
/// @param e The effects (borrowed).
/// @return false if the view lets clicks through. False on failure.
ELG_EXPORT bool elg_effects_allows_hit_testing(const Effects* e) {
    ELG_TRY
    return e->allows_hit_testing;
    ELG_CATCH(false)
}
/// Sets Effects::allows_hit_testing.
///
/// @param e The effects (borrowed).
/// @param enabled false: the view isn't hit, and clicks go to what's behind it.
ELG_EXPORT void elg_effects_set_allows_hit_testing(Effects* e, bool enabled) {
    ELG_TRY
    e->allows_hit_testing = enabled;
    ELG_CATCH_VOID
}

// --- Prop handles ---

/// Reads a float prop handle (an Effects field): a tracked read.
///
/// @param prop The prop (borrowed).
/// @param environment The environment it reads, for a prop that depends on one (borrowed); may be null for
///   one that doesn't, and then such a prop is a usage error.
/// @return The value; 0 on failure.
ELG_EXPORT float elg_prop_f32_get(const Prop<float>* prop, const EnvironmentValues* environment) {
    ELG_TRY
    return environment ? prop->get(*environment) : prop->get();
    ELG_CATCH(0.f)
}
/// Copies a float prop handle: both refer to the same prop, reactive as it is.
///
/// @param prop The prop (borrowed).
/// @return The copy, owned by the caller (elg_prop_f32_free()). Null on failure.
ELG_EXPORT Prop<float>* elg_prop_f32_clone(const Prop<float>* prop) {
    ELG_TRY
    return new Prop<float>(*prop);
    ELG_CATCH(nullptr)
}
/// Frees a float prop handle.
///
/// @param prop The prop (consumed); may be null.
ELG_EXPORT void elg_prop_f32_free(Prop<float>* prop) {
    const Consumed consumed(prop); // freed as the call ends
    ELG_TRY
    ELG_CATCH_VOID
}
/// Reads a color prop handle (an Effects field): a tracked read.
///
/// @param prop The prop (borrowed).
/// @param environment The environment it reads, for a prop that depends on one, such as a theme color
///   (borrowed); may be null for one that doesn't, and then such a prop is a usage error.
/// @return The color; all zeros on failure.
ELG_EXPORT ElgColor elg_prop_color_get(const Prop<Color>* prop, const EnvironmentValues* environment) {
    ELG_TRY
    return c_color(environment ? prop->get(*environment) : prop->get());
    ELG_CATCH({})
}
/// Copies a color prop handle: both refer to the same prop, reactive as it is.
///
/// @param prop The prop (borrowed).
/// @return The copy, owned by the caller (elg_prop_color_free()). Null on failure.
ELG_EXPORT Prop<Color>* elg_prop_color_clone(const Prop<Color>* prop) {
    ELG_TRY
    return new Prop<Color>(*prop);
    ELG_CATCH(nullptr)
}
/// Frees a color prop handle.
///
/// @param prop The prop (consumed); may be null.
ELG_EXPORT void elg_prop_color_free(Prop<Color>* prop) {
    const Consumed consumed(prop); // freed as the call ends
    ELG_TRY
    ELG_CATCH_VOID
}

// --- Reading effects ---

/// Reads Effects::opacity, so that a modifier can build on it (with an ElgProp of kind 4, or a closure that
/// reads it).
///
/// @param e The effects (borrowed).
/// @return The opacity, a prop handle owned by the caller (elg_prop_f32_free()). Null on failure.
ELG_EXPORT Prop<float>* elg_effects_opacity(const Effects* e) {
    ELG_TRY
    return new Prop<float>(e->opacity);
    ELG_CATCH(nullptr)
}
/// Reads Effects::offset_x and Effects::offset_y.
///
/// @param e The effects (borrowed).
/// @param x Receives the horizontal offset, a prop handle owned by the caller (elg_prop_f32_free()).
/// @param y Receives the vertical offset, likewise.
ELG_EXPORT void elg_effects_offset(const Effects* e, Prop<float>** x, Prop<float>** y) {
    ELG_TRY
    auto offset_x = std::make_unique<Prop<float>>(e->offset_x);
    auto offset_y = std::make_unique<Prop<float>>(e->offset_y);
    *x = offset_x.release();
    *y = offset_y.release();
    ELG_CATCH_VOID
}
/// Reads Effects::blur and Effects::blur_opaque.
///
/// @param e The effects (borrowed).
/// @param radius Receives the radius in points, a prop handle owned by the caller (elg_prop_f32_free()), when
///   there's a blur; left as it was otherwise.
/// @return Whether there's a blur; false on failure.
ELG_EXPORT bool elg_effects_blur(const Effects* e, Prop<float>** radius) {
    ELG_TRY
    if (!e->blur) {
        return false;
    }
    *radius = new Prop<float>(*e->blur);
    return true;
    ELG_CATCH(false)
}
/// Reads Effects::blur_opaque: the content fills its bounds, so the blur's edges stay sharp.
///
/// @param e The effects (borrowed).
/// @return The flag; false on failure.
ELG_EXPORT bool elg_effects_blur_opaque(const Effects* e) {
    ELG_TRY
    return e->blur_opaque;
    ELG_CATCH(false)
}
/// Reads Effects::rotation.
///
/// @param e The effects (borrowed).
/// @param degrees Receives the rotation in degrees, clockwise, a prop handle owned by the caller
///   (elg_prop_f32_free()), when there's one; left as it was otherwise.
/// @return Whether there's a rotation; false on failure.
ELG_EXPORT bool elg_effects_rotation(const Effects* e, Prop<float>** degrees) {
    ELG_TRY
    if (!e->rotation) {
        return false;
    }
    *degrees = new Prop<float>(*e->rotation);
    return true;
    ELG_CATCH(false)
}
/// Reads Effects::scale_x and Effects::scale_y.
///
/// @param e The effects (borrowed).
/// @param x Receives the horizontal scale, a prop handle owned by the caller (elg_prop_f32_free()), or null
///   when there's none.
/// @param y Receives the vertical scale, likewise.
ELG_EXPORT void elg_effects_scale(const Effects* e, Prop<float>** x, Prop<float>** y) {
    ELG_TRY
    auto scale_x = e->scale_x ? std::make_unique<Prop<float>>(*e->scale_x) : nullptr;
    auto scale_y = e->scale_y ? std::make_unique<Prop<float>>(*e->scale_y) : nullptr;
    *x = scale_x.release();
    *y = scale_y.release();
    ELG_CATCH_VOID
}
/// Reads Effects::z_index.
///
/// @param e The effects (borrowed).
/// @param z Receives the stacking order, a prop handle owned by the caller (elg_prop_f32_free()), when it's
///   set; left as it was otherwise.
/// @return Whether it's set; false on failure.
ELG_EXPORT bool elg_effects_z_index(const Effects* e, Prop<float>** z) {
    ELG_TRY
    if (!e->z_index) {
        return false;
    }
    *z = new Prop<float>(*e->z_index);
    return true;
    ELG_CATCH(false)
}
/// Reads Effects::clip_shape.
///
/// @param e The effects (borrowed).
/// @param shape Receives the shape, owned by the caller (elg_shape_free()), when there's one; left as it was
///   otherwise.
/// @return Whether there's a clip shape; false on failure.
ELG_EXPORT bool elg_effects_clip_shape(const Effects* e, Shape** shape) {
    ELG_TRY
    if (!e->clip_shape) {
        return false;
    }
    *shape = new Shape(*e->clip_shape);
    return true;
    ELG_CATCH(false)
}
/// Reads Effects::content_shape: the shape hit testing uses.
///
/// @param e The effects (borrowed).
/// @param shape Receives the shape, owned by the caller (elg_shape_free()), when there's one; left as it was
///   otherwise.
/// @return Whether there's a content shape; false on failure.
ELG_EXPORT bool elg_effects_content_shape(const Effects* e, Shape** shape) {
    ELG_TRY
    if (!e->content_shape) {
        return false;
    }
    *shape = new Shape(*e->content_shape);
    return true;
    ELG_CATCH(false)
}
/// Reads Effects::shadow.
///
/// @param e The effects (borrowed).
/// @param shadow Receives the shadow when there's one; left as it was otherwise.
/// @return Whether there's a shadow; false on failure.
ELG_EXPORT bool elg_effects_shadow(const Effects* e, ElgShadow* shadow) {
    ELG_TRY
    if (!e->shadow) {
        return false;
    }
    *shadow = c_shadow(*e->shadow);
    return true;
    ELG_CATCH(false)
}
/// Reads Effects::inner_shadow.
///
/// @param e The effects (borrowed).
/// @param shadow Receives the shadow when there's one; left as it was otherwise.
/// @return Whether there's an inner shadow; false on failure.
ELG_EXPORT bool elg_effects_inner_shadow(const Effects* e, ElgShadow* shadow) {
    ELG_TRY
    if (!e->inner_shadow) {
        return false;
    }
    *shadow = c_shadow(*e->inner_shadow);
    return true;
    ELG_CATCH(false)
}
/// Reads Effects::color_filter.
///
/// @param e The effects (borrowed).
/// @param kind Receives the filter's ColorFilter::Kind, by its position in the enum, when there's one.
/// @param amount Receives its amount, a prop handle owned by the caller (elg_prop_f32_free()), likewise.
/// @param color Receives its color, a prop handle owned by the caller (elg_prop_color_free()), likewise.
/// @return Whether there's a color filter; false on failure, when nothing is written.
ELG_EXPORT bool elg_effects_color_filter(const Effects* e, int32_t* kind, Prop<float>** amount, Prop<Color>** color) {
    ELG_TRY
    if (!e->color_filter) {
        return false;
    }
    auto filter_amount = std::make_unique<Prop<float>>(e->color_filter->amount);
    auto filter_color = std::make_unique<Prop<Color>>(e->color_filter->color);
    *kind = static_cast<int32_t>(e->color_filter->kind);
    *amount = filter_amount.release();
    *color = filter_color.release();
    return true;
    ELG_CATCH(false)
}
/// Adds to Effects::on_size_change: `on_size_change` is called after the handler the effects already have.
/// Called after the first layout and whenever the view's size in pixels changes, on the UI thread, before
/// paint.
///
/// @param e The effects (borrowed).
/// @param on_size_change args: ElgPixelSize* (the logical size times the display scale, rounded up; may be
///   zero). Required: none is a usage error.
ELG_EXPORT void elg_effects_add_on_size_change(Effects* e, ElgClosure on_size_change) {
    const Consumed consumed(on_size_change);
    ELG_TRY
    const SharedFn f = required(on_size_change, "on_size_change");
    e->on_size_change = [before = std::move(e->on_size_change), f](PixelSize size) {
        if (before) {
            before(size);
        }
        ElgPixelSize arg{size.width, size.height};
        f->call(&arg, nullptr);
    };
    ELG_CATCH_VOID
}

// --- Event handlers ---

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
    [[nodiscard]] EventPhase phase() const override {
        ElgEventCall call{};
        call.op = 14;
        int32_t phase = 1; // bubble, unless the handler says otherwise
        f_->call(&call, &phase);
        return phase == 0 ? EventPhase::capture : EventPhase::bubble;
    }

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
    const Consumed consumed(make_handler);
    ELG_TRY
    return out(v->modifier(RustEventModifier(required(make_handler, "make_handler"))));
    ELG_CATCH(nullptr)
}

// --- ABI layout ---

/// Describes the layout of this file's structs, for a binding's layout test: one line per struct,
/// "name size align", then " field:offset" for each of its fields, in order.
///
/// @param out Receives each line.
/// @param ctx Passed to `out`.
ELG_EXPORT void elg_layouts_ext(ElgStrSink out, void* ctx) {
    ELG_TRY
    ELG_LAYOUT(ElgLayoutCall);
    ELG_LAYOUT(ElgLayoutModifierCall);
    ELG_LAYOUT(ElgEventCall);
    ELG_LAYOUT(ElgOptHoverDelay);
    ELG_CATCH_VOID
}
