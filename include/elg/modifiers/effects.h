// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MODIFIERS_EFFECTS_H
#define ELG_MODIFIERS_EFFECTS_H

// Built-in effect modifiers. View::clipped(), clip_shape(), opacity(), shadow() are sugar over
// these.

#include <elg/export.h>
#include <elg/modifier.h>
#include <elg/shapes.h>
#include <elg/view.h>

#include <utility>

namespace elg {

class ELG_API ClippedModifier final : public EffectModifier {
public:
    void apply(Effects& effects) const override { effects.clip_to_bounds = true; }
};

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
