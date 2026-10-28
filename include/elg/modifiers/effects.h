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

class ELG_API ClipShapeModifier final : public EffectModifier {
public:
    explicit ClipShapeModifier(Shape shape) : shape_(std::move(shape)) {}
    void apply(Effects& effects) const override { effects.clip_shape = shape_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    Shape shape_;
    ELG_PRIVATE_MEMBERS_END
};

class ELG_API OpacityModifier final : public EffectModifier {
public:
    explicit OpacityModifier(Prop<float> opacity) : opacity_(std::move(opacity)) {}
    void apply(Effects& effects) const override { effects.opacity = opacity_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    Prop<float> opacity_;
    ELG_PRIVATE_MEMBERS_END
};

class ELG_API ShadowModifier final : public EffectModifier {
public:
    explicit ShadowModifier(Shadow shadow) noexcept : shadow_(shadow) {}
    void apply(Effects& effects) const override { effects.shadow = shadow_; }

private:
    Shadow shadow_;
};

class ELG_API InnerShadowModifier final : public EffectModifier {
public:
    explicit InnerShadowModifier(Shadow shadow) noexcept : shadow_(shadow) {}
    void apply(Effects& effects) const override { effects.inner_shadow = shadow_; }

private:
    Shadow shadow_;
};

class ELG_API BlurModifier final : public EffectModifier {
public:
    BlurModifier(Prop<float> radius, bool opaque) : radius_(std::move(radius)), opaque_(opaque) {}
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

class ELG_API ColorFilterModifier final : public EffectModifier {
public:
    explicit ColorFilterModifier(ColorFilter filter) : filter_(std::move(filter)) {}
    void apply(Effects& effects) const override { effects.color_filter = filter_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    ColorFilter filter_;
    ELG_PRIVATE_MEMBERS_END
};

class ELG_API BlendModeModifier final : public EffectModifier {
public:
    explicit BlendModeModifier(BlendMode mode) noexcept : mode_(mode) {}
    void apply(Effects& effects) const override { effects.blend_mode = mode_; }

private:
    BlendMode mode_;
};

class ELG_API CompositingGroupModifier final : public EffectModifier {
public:
    void apply(Effects& effects) const override { effects.compositing_group = true; }
};

// Rotation (degrees, clockwise) about a unit point of the view's bounds. Reactive and animatable.
class ELG_API RotationModifier final : public EffectModifier {
public:
    RotationModifier(Prop<float> degrees, UnitPoint anchor) : degrees_(std::move(degrees)), anchor_(anchor) {}
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

class ELG_API ScaleModifier final : public EffectModifier {
public:
    ScaleModifier(Prop<float> x, Prop<float> y, UnitPoint anchor) : x_(std::move(x)), y_(std::move(y)), anchor_(anchor) {}
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

// Stacking order among siblings: higher draws in front (and is hit first). Default 0; ties keep
// declaration order. Reactive.
class ELG_API ZIndexModifier final : public EffectModifier {
public:
    explicit ZIndexModifier(Prop<float> z) : z_(std::move(z)) {}
    void apply(Effects& effects) const override { effects.z_index = z_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    Prop<float> z_;
    ELG_PRIVATE_MEMBERS_END
};

// Moves the drawn content without affecting layout. Reactive and animatable.
class ELG_API OffsetModifier final : public EffectModifier {
public:
    OffsetModifier(Prop<float> x, Prop<float> y) : x_(std::move(x)), y_(std::move(y)) {}
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

// Hit tests against a shape filling the view's bounds instead of the drawn content.
class ELG_API ContentShapeModifier final : public EffectModifier {
public:
    explicit ContentShapeModifier(Shape shape) : shape_(std::move(shape)) {}
    void apply(Effects& effects) const override { effects.content_shape = shape_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    Shape shape_;
    ELG_PRIVATE_MEMBERS_END
};

class ELG_API AllowsHitTestingModifier final : public EffectModifier {
public:
    explicit AllowsHitTestingModifier(bool enabled) noexcept : enabled_(enabled) {}
    void apply(Effects& effects) const override { effects.allows_hit_testing = enabled_; }

private:
    bool enabled_;
};

} // namespace elg

#endif // ELG_MODIFIERS_EFFECTS_H
