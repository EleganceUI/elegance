// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MODIFIERS_EFFECTS_H
#define ELG_MODIFIERS_EFFECTS_H

// Built-in effect modifiers. View::clip_shape(), opacity(), shadow(), blur(), the
// color adjustments, blend_mode(), rotation_effect() and scale_effect() are sugar over these.

#include <elg/export.h>
#include <elg/modifier.h>
#include <elg/shapes.h>
#include <elg/view.h>

#include <utility>

namespace elg {

/// Clips the content to a shape filling the view's bounds (View::clip_shape()).
class ELG_API ClipShapeModifier final : public EffectModifier {
public:
    /// Clips to `shape`.
    ///
    /// @param shape The shape to clip to.
    explicit ClipShapeModifier(Shape shape) : shape_(std::move(shape)) {}
    /// Sets Effects::clip_shape.
    ///
    /// @param effects The effects to change.
    void apply(Effects& effects) const override { effects.clip_shape = shape_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    Shape shape_;
    ELG_PRIVATE_MEMBERS_END
};

/// Fades the view as a group (View::opacity()).
class ELG_API OpacityModifier final : public EffectModifier {
public:
    /// Fades to `opacity`.
    ///
    /// @param opacity 0 (invisible) to 1 (unchanged). Reactive and animatable.
    explicit OpacityModifier(Prop<float> opacity) : opacity_(std::move(opacity)) {}
    /// Sets Effects::opacity.
    ///
    /// @param effects The effects to change.
    void apply(Effects& effects) const override { effects.opacity = opacity_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    Prop<float> opacity_;
    ELG_PRIVATE_MEMBERS_END
};

/// A blurred shadow of the view's content (View::shadow()).
class ELG_API ShadowModifier final : public EffectModifier {
public:
    /// Casts `shadow`.
    ///
    /// @param shadow The shadow's color, blur radius, offset and spread.
    explicit ShadowModifier(Shadow shadow) noexcept : shadow_(shadow) {}
    /// Sets Effects::shadow.
    ///
    /// @param effects The effects to change.
    void apply(Effects& effects) const override { effects.shadow = shadow_; }

private:
    Shadow shadow_;
};

/// A shadow inside the view's content, from its edges inward (View::inner_shadow()).
class ELG_API InnerShadowModifier final : public EffectModifier {
public:
    /// Casts `shadow` inside.
    ///
    /// @param shadow The shadow's color, blur radius, offset and spread.
    explicit InnerShadowModifier(Shadow shadow) noexcept : shadow_(shadow) {}
    /// Sets Effects::inner_shadow.
    ///
    /// @param effects The effects to change.
    void apply(Effects& effects) const override { effects.inner_shadow = shadow_; }

private:
    Shadow shadow_;
};

/// Gaussian blur of the content (View::blur()).
class ELG_API BlurModifier final : public EffectModifier {
public:
    /// Blurs by `radius`.
    ///
    /// @param radius The blur radius in points. Reactive and animatable.
    /// @param opaque Whether the content fills its bounds: its edges then stay sharp.
    BlurModifier(Prop<float> radius, bool opaque) : radius_(std::move(radius)), opaque_(opaque) {}
    /// Sets Effects::blur and Effects::blur_opaque.
    ///
    /// @param effects The effects to change.
    void apply(Effects& effects) const override {
        effects.blur = radius_;
        effects.blur_opaque = opaque_;
    }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    Prop<float> radius_;
    bool opaque_;
    ELG_PRIVATE_MEMBERS_END
};

/// A color adjustment of the content (View::brightness(), saturation(), ...).
class ELG_API ColorFilterModifier final : public EffectModifier {
public:
    /// Applies `filter`.
    ///
    /// @param filter The adjustment and its amount.
    explicit ColorFilterModifier(ColorFilter filter) : filter_(std::move(filter)) {}
    /// Sets Effects::color_filter.
    ///
    /// @param effects The effects to change.
    void apply(Effects& effects) const override { effects.color_filter = filter_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    ColorFilter filter_;
    ELG_PRIVATE_MEMBERS_END
};

/// Blends the view with what's behind it (View::blend_mode()).
class ELG_API BlendModeModifier final : public EffectModifier {
public:
    /// Blends with `mode`.
    ///
    /// @param mode How the view's pixels combine with what's behind them.
    explicit BlendModeModifier(BlendMode mode) noexcept : mode_(mode) {}
    /// Sets Effects::blend_mode.
    ///
    /// @param effects The effects to change.
    void apply(Effects& effects) const override { effects.blend_mode = mode_; }

private:
    BlendMode mode_;
};

/// Draws the content offscreen first, then composites it as one image (View::compositing_group()).
class ELG_API CompositingGroupModifier final : public EffectModifier {
public:
    /// Sets Effects::compositing_group.
    ///
    /// @param effects The effects to change.
    void apply(Effects& effects) const override { effects.compositing_group = true; }
};

/// Rotation (degrees, clockwise) about a unit point of the view's bounds. Reactive and animatable.
class ELG_API RotationModifier final : public EffectModifier {
public:
    /// Rotates by `degrees` about `anchor`.
    ///
    /// @param degrees The angle, clockwise.
    /// @param anchor The point it turns about, in unit coordinates of the view's bounds.
    RotationModifier(Prop<float> degrees, UnitPoint anchor) : degrees_(std::move(degrees)), anchor_(anchor) {}
    /// Sets Effects::rotation and Effects::anchor.
    ///
    /// @param effects The effects to change.
    void apply(Effects& effects) const override {
        effects.rotation = degrees_;
        effects.anchor = anchor_;
    }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    Prop<float> degrees_;
    UnitPoint anchor_;
    ELG_PRIVATE_MEMBERS_END
};

/// Scales the drawn content about a unit point of the view's bounds (View::scale_effect()).
/// Reactive and animatable.
class ELG_API ScaleModifier final : public EffectModifier {
public:
    /// Scales by (`x`, `y`) about `anchor`.
    ///
    /// @param x The horizontal factor (1 unchanged).
    /// @param y The vertical factor (1 unchanged).
    /// @param anchor The point that stays put, in unit coordinates of the view's bounds.
    ScaleModifier(Prop<float> x, Prop<float> y, UnitPoint anchor) : x_(std::move(x)), y_(std::move(y)), anchor_(anchor) {}
    /// Sets Effects::scale_x, Effects::scale_y and Effects::anchor.
    ///
    /// @param effects The effects to change.
    void apply(Effects& effects) const override {
        effects.scale_x = x_;
        effects.scale_y = y_;
        effects.anchor = anchor_;
    }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    Prop<float> x_;
    Prop<float> y_;
    UnitPoint anchor_;
    ELG_PRIVATE_MEMBERS_END
};

/// Stacking order among siblings: higher draws in front (and is hit first). Default 0; ties keep
/// declaration order. Reactive.
class ELG_API ZIndexModifier final : public EffectModifier {
public:
    /// Stacks at `z`.
    ///
    /// @param z The order among siblings.
    explicit ZIndexModifier(Prop<float> z) : z_(std::move(z)) {}
    /// Sets Effects::z_index.
    ///
    /// @param effects The effects to change.
    void apply(Effects& effects) const override { effects.z_index = z_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    Prop<float> z_;
    ELG_PRIVATE_MEMBERS_END
};

/// Moves the drawn content without affecting layout. Reactive and animatable.
class ELG_API OffsetModifier final : public EffectModifier {
public:
    /// Moves by (`x`, `y`).
    ///
    /// @param x Points to move right (negative: left).
    /// @param y Points to move down (negative: up).
    OffsetModifier(Prop<float> x, Prop<float> y) : x_(std::move(x)), y_(std::move(y)) {}
    /// Sets Effects::offset_x and Effects::offset_y.
    ///
    /// @param effects The effects to change.
    void apply(Effects& effects) const override {
        effects.offset_x = x_;
        effects.offset_y = y_;
    }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    Prop<float> x_;
    Prop<float> y_;
    ELG_PRIVATE_MEMBERS_END
};

/// Hit tests against a shape filling the view's bounds instead of the drawn content.
class ELG_API ContentShapeModifier final : public EffectModifier {
public:
    /// Hit tests against `shape`.
    ///
    /// @param shape The area that takes the pointer.
    explicit ContentShapeModifier(Shape shape) : shape_(std::move(shape)) {}
    /// Sets Effects::content_shape.
    ///
    /// @param effects The effects to change.
    void apply(Effects& effects) const override { effects.content_shape = shape_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    Shape shape_;
    ELG_PRIVATE_MEMBERS_END
};

/// Lets the pointer through the view and everything in it, or not (View::allows_hit_testing()).
class ELG_API AllowsHitTestingModifier final : public EffectModifier {
public:
    /// Hit tests the view only if `enabled`.
    ///
    /// @param enabled false: the view and everything in it ignore the pointer.
    explicit AllowsHitTestingModifier(bool enabled) noexcept : enabled_(enabled) {}
    /// Sets Effects::allows_hit_testing.
    ///
    /// @param effects The effects to change.
    void apply(Effects& effects) const override { effects.allows_hit_testing = enabled_; }

private:
    bool enabled_;
};

} // namespace elg

#endif // ELG_MODIFIERS_EFFECTS_H
