// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MODIFIER_H
#define ELG_MODIFIER_H

// The modifier protocols (design.md §9.4). Every modifier, built-in or user-defined,
// implements exactly one of these; the engine lowers each protocol differently.

#include <elg/environment.h>
#include <elg/events.h>
#include <elg/export.h>
#include <elg/geometry.h>
#include <elg/prop.h>
#include <elg/shapes.h>
#include <elg/view.h>

#include <memory>
#include <typeinfo>
#include <optional>

namespace elg {

namespace detail {
class Element;
}

/// Which protocol a Modifier implements: one per protocol class (ViewModifier, LayoutModifier,
/// DecorationModifier, EnvironmentModifier, EffectModifier, EventModifier).
enum class ModifierProtocol { view, layout, decoration, environment, effect, event };

/// The base of every modifier. Derive from one of the protocol classes below, not from this;
/// View::modifier() applies one to a view.
class ELG_API Modifier {
public:
    virtual ~Modifier();
    /// Which protocol this is. Stable, RTTI-free dispatch (hot reload rule 2, design.md §9.6).
    ///
    /// @return The protocol; each protocol class below returns its own.
    [[nodiscard]] virtual ModifierProtocol protocol() const noexcept = 0;

protected:
    // Modifiers are copied as their concrete type (into the views using them), never as a Modifier.
    Modifier(const Modifier&) = default;
    Modifier(Modifier&&) = default;
    Modifier& operator=(const Modifier&) = default;
    Modifier& operator=(Modifier&&) = default;

private:
    // Only the protocol classes below derive from Modifier directly.
    Modifier() = default;
    friend class ViewModifier;
    friend class LayoutModifier;
    friend class DecorationModifier;
    friend class EnvironmentModifier;
    friend class EffectModifier;
    friend class EventModifier;
};

/// Composes other modifiers. Most user modifiers are this kind:
///
/// @code
/// struct Card : elg::ViewModifier {
///     View body(View content) const override { return content.padding(16).background(...); }
/// };
/// @endcode
class ELG_API ViewModifier : public Modifier {
public:
    /// @return ModifierProtocol::view.
    [[nodiscard]] ModifierProtocol protocol() const noexcept final { return ModifierProtocol::view; }
    /// Called once, when the view is mounted (each time it is).
    ///
    /// @param content The modified view.
    /// @return What to show instead: usually `content` with modifiers applied.
    [[nodiscard]] virtual View body(View content) const = 0;
};

/// A child as seen by a layout or layout modifier: it can be measured and placed.
class ELG_API LayoutChild {
public:
    /// Internal: created by the framework.
    ///
    /// @param element The child's element; must outlive this.
    explicit LayoutChild(detail::Element& element) noexcept;

    /// Measures the child.
    ///
    /// @param proposal The size offered to it.
    /// @return The size it takes for `proposal`.
    [[nodiscard]] Size size_that_fits(ProposedSize proposal) const;
    /// Positions the child's top-left corner; it sizes itself from `proposal`.
    ///
    /// @param position Where its top-left corner goes, in the parent's coordinates.
    /// @param proposal The size offered to it.
    void place(Point position, ProposedSize proposal) const;
    /// @return The child's layout priority (View::layout_priority(); 0 by default).
    [[nodiscard]] float layout_priority() const noexcept;
    /// @return Whether the child is a Spacer.
    [[nodiscard]] bool is_spacer() const noexcept;

    /// The value set on this child with View::layout_value<Key>(...), else Key::default_value().
    ///
    /// @tparam Key The key.
    /// @return The child's value for `Key`.
    template <class Key>
    [[nodiscard]] typename Key::Value value() const {
        if (const void* found = layout_value(typeid(Key))) {
            return *static_cast<const typename Key::Value*>(found);
        }
        return Key::default_value();
    }

private:
    [[nodiscard]] const void* layout_value(const std::type_info& key) const;

    detail::Element* element_;
};

/// Takes part in layout, wrapping one child (padding, frame, ...).
class ELG_API LayoutModifier : public Modifier {
public:
    /// @return ModifierProtocol::layout.
    [[nodiscard]] ModifierProtocol protocol() const noexcept final { return ModifierProtocol::layout; }

    /// The size of the modified view for `proposal`, usually from measuring `content`.
    ///
    /// @param proposal The size the parent offers.
    /// @param content The modified view's content.
    /// @return The size the modified view takes.
    [[nodiscard]] virtual Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const = 0;
    /// Places `content` within `bounds`.
    ///
    /// @param bounds Has the size this modifier reported for `proposal`, positioned by the parent.
    /// @param proposal The size the parent offered.
    /// @param content The modified view's content, to place.
    virtual void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const = 0;
    /// Overrides the content's layout priority when set.
    ///
    /// @return The modified view's layout priority, or nullopt for the content's.
    [[nodiscard]] virtual std::optional<float> layout_priority() const { return std::nullopt; }
};

/// Carries a value for the parent layout (View::layout_value) without changing layout.
class ELG_API LayoutValueModifierBase : public LayoutModifier {
public:
    /// The content's size: this modifier doesn't change layout.
    ///
    /// @param proposal The size the parent offers.
    /// @param content The modified view's content.
    /// @return The content's size for `proposal`.
    [[nodiscard]] Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const final {
        return content.size_that_fits(proposal);
    }
    /// Places the content at the bounds' origin with the parent's proposal.
    ///
    /// @param bounds Where the parent put the modified view.
    /// @param proposal The size the parent offered.
    /// @param content The modified view's content.
    void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const final {
        content.place({bounds.x, bounds.y}, proposal);
    }
    /// The value for `key`, if this modifier carries it.
    ///
    /// @param key The key's type, `typeid(Key)`.
    /// @return A pointer to the `Key::Value` this modifier holds, or nullptr for another key.
    [[nodiscard]] virtual const void* value_for(const std::type_info& key) const noexcept = 0;
};

/// The modifier View::layout_value<Key>() applies: carries `value` for `Key`.
///
/// @tparam Key The key: a type with a `Value` type and a `default_value()`.
template <class Key>
class LayoutValueModifier final : public LayoutValueModifierBase {
public:
    /// @param value The value for `Key`.
    explicit LayoutValueModifier(typename Key::Value value) : value_(std::move(value)) {}
    /// The value for `key`, if it's `Key`.
    ///
    /// @param key The key's type.
    /// @return The value for `Key` if `key` is `typeid(Key)`, else nullptr.
    [[nodiscard]] const void* value_for(const std::type_info& key) const noexcept override {
        return key == typeid(Key) ? &value_ : nullptr;
    }

private:
    typename Key::Value value_;
};

/// Adds a secondary view sized to the content: drawn behind it (background) or in front of
/// it (overlay). The secondary view doesn't affect layout: it's offered exactly the content's
/// size and aligned within it.
class ELG_API DecorationModifier : public Modifier {
public:
    /// Where the secondary view goes: `behind` the content (hits go to the content first),
    /// `in_front` of it (hits go to the decoration first), or `mask`: the decoration isn't drawn;
    /// its alpha masks the content.
    enum class Placement { behind, in_front, mask };

    /// @return ModifierProtocol::decoration.
    [[nodiscard]] ModifierProtocol protocol() const noexcept final { return ModifierProtocol::decoration; }

    /// @return The secondary view.
    [[nodiscard]] virtual View decoration() const = 0;
    /// @return Where the secondary view goes.
    [[nodiscard]] virtual Placement placement() const = 0;
    /// @return Where the secondary view sits when its size differs from the content's (centered by
    ///   default).
    [[nodiscard]] virtual Alignment alignment() const { return Alignment::center; }
};

/// Changes inherited values for everything inside the modified view (font, foreground, ...).
class ELG_API EnvironmentModifier : public Modifier {
public:
    /// @return ModifierProtocol::environment.
    [[nodiscard]] ModifierProtocol protocol() const noexcept final { return ModifierProtocol::environment; }
    /// Sets values for everything inside. Called when the view is mounted.
    ///
    /// @param values A copy of the environment from outside the view, to change.
    virtual void apply(EnvironmentValues& values) const = 0;
};

/// A color adjustment (View::brightness, saturation, ...), applied to the content's pixels in sRGB
/// with straight alpha, as CSS filter functions are.
struct ColorFilter {
    /// Which adjustment: the View method of the same name (invert is color_invert(), multiply is
    /// color_multiply()).
    enum class Kind { brightness, contrast, saturation, grayscale, hue_rotation, sepia, invert, multiply, luminance_to_alpha };
    Kind kind = Kind::saturation; ///< Which adjustment.
    Prop<float> amount = 1.f;      ///< Degrees for hue_rotation; unused for multiply, luminance_to_alpha.
    Prop<Color> color = Color::white; ///< Multiply only.
};

/// How the modified view's content is drawn and hit tested (design.md §9.4). Drawing applies
/// the clip first, then the transform, then the rest to the clipped result.
struct Effects {
    /// Post-layout physical size observation (View::on_size_change()); state is kept per mounted
    /// view.
    std::function<void(PixelSize)> on_size_change;
    std::optional<Shape> clip_shape; ///< Fills the view's bounds.
    Prop<float> opacity = 1.f;       ///< 0 to 1; the content fades as a group.
    std::optional<Shadow> shadow;       ///< A drop shadow of the content.
    std::optional<Shadow> inner_shadow; ///< A shadow inside the content, from its edges inward.
    std::optional<Prop<float>> blur; ///< Radius in points.
    bool blur_opaque = false;        ///< The content fills its bounds: blurred edges stay sharp.
    std::optional<ColorFilter> color_filter; ///< A color adjustment.
    BlendMode blend_mode = BlendMode::normal; ///< How the content blends with what's behind it.
    bool compositing_group = false; ///< Offscreen even when nothing else needs it.
    /// Rotation (degrees, clockwise) of the drawn content about `anchor`.
    std::optional<Prop<float>> rotation;
    std::optional<Prop<float>> scale_x; ///< Horizontal scale of the drawn content about `anchor`.
    std::optional<Prop<float>> scale_y; ///< Vertical scale of the drawn content about `anchor`.
    UnitPoint anchor = UnitPoint::center; ///< What rotation and scale turn about.
    /// Moves the drawn content (and where it's hit) without affecting layout.
    Prop<float> offset_x = 0.f;
    Prop<float> offset_y = 0.f; ///< Like `offset_x`, down.
    /// Stacking order among siblings, when set.
    std::optional<Prop<float>> z_index;
    /// Hit testing: by default a view is hit where its content draws (shapes by their outline,
    /// text by its frame), within any clip. When set, this shape (filling the bounds) is hit
    /// instead.
    std::optional<Shape> content_shape;
    bool allows_hit_testing = true; ///< false: the view and everything in it ignore the pointer.
};

/// Changes how the modified view's content is drawn and hit tested, not its layout.
class ELG_API EffectModifier : public Modifier {
public:
    /// @return ModifierProtocol::effect.
    [[nodiscard]] ModifierProtocol protocol() const noexcept final { return ModifierProtocol::effect; }
    /// Sets the effects it applies. Called when the view is mounted; each effect modifier gets
    /// its own Effects, so chained ones apply one after the other.
    ///
    /// @param effects Default effects (none), to change.
    virtual void apply(Effects& effects) const = 0;
};

/// Handles input for the modified view: makes one EventHandler per mounted view (design.md §10).
class ELG_API EventModifier : public Modifier {
public:
    /// @return ModifierProtocol::event.
    [[nodiscard]] ModifierProtocol protocol() const noexcept final { return ModifierProtocol::event; }
    /// Called when the view is mounted (each time it is).
    ///
    /// @return A new handler for that mounted view.
    [[nodiscard]] virtual std::unique_ptr<EventHandler> make_handler() const = 0;
};

} // namespace elg

#endif // ELG_MODIFIER_H
