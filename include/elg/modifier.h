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
#include <optional>

namespace elg {

namespace detail {
class Element;
}

enum class ModifierProtocol { view, layout, decoration, environment, effect, event };

class ELG_API Modifier {
public:
    virtual ~Modifier();
    // Stable, RTTI-free dispatch (hot reload rule 2, design.md §9.6).
    virtual ModifierProtocol protocol() const noexcept = 0;

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

// Composes other modifiers. Most user modifiers are this kind:
//   struct Card : elg::ViewModifier {
//       View body(View content) const override { return content.padding(16).background(...); }
//   };
class ELG_API ViewModifier : public Modifier {
public:
    ModifierProtocol protocol() const noexcept final { return ModifierProtocol::view; }
    // Called once, when the view is mounted.
    virtual View body(View content) const = 0;
};

// A child as seen by a layout or layout modifier: it can be measured and placed.
class ELG_API LayoutChild {
public:
    // Internal: created by the framework.
    explicit LayoutChild(detail::Element& element) noexcept;

    Size size_that_fits(ProposedSize proposal) const;
    // Positions the child's top-left corner; it sizes itself from `proposal`.
    void place(Point position, ProposedSize proposal) const;
    float layout_priority() const noexcept;
    bool is_spacer() const noexcept;

private:
    detail::Element* element_;
};

// Takes part in layout, wrapping one child (padding, frame, ...).
class ELG_API LayoutModifier : public Modifier {
public:
    ModifierProtocol protocol() const noexcept final { return ModifierProtocol::layout; }

    virtual Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const = 0;
    // `bounds` has the size this modifier reported for `proposal`, positioned by the parent.
    virtual void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const = 0;
    // Overrides the content's layout priority when set.
    virtual std::optional<float> layout_priority() const { return std::nullopt; }
};

// Adds a secondary view sized to the content: drawn behind it (background) or in front of
// it (overlay). The secondary view doesn't affect layout.
class ELG_API DecorationModifier : public Modifier {
public:
    enum class Placement { behind, in_front };

    ModifierProtocol protocol() const noexcept final { return ModifierProtocol::decoration; }

    virtual View decoration() const = 0;
    virtual Placement placement() const = 0;
    virtual Alignment alignment() const { return Alignment::center; }
};

// Changes inherited values for everything inside the modified view (font, foreground, ...).
class ELG_API EnvironmentModifier : public Modifier {
public:
    ModifierProtocol protocol() const noexcept final { return ModifierProtocol::environment; }
    virtual void apply(EnvironmentValues& values) const = 0;
};

// How the modified view's content is drawn and hit tested (design.md §9.4). Drawing applies
// the clip first, then opacity and shadow of the clipped result.
struct Effects {
    bool clip_to_bounds = false;
    std::optional<Shape> clip_shape; // fills the view's bounds
    Prop<float> opacity = 1.f;
    std::optional<Shadow> shadow;
    // Moves the drawn content (and where it's hit) without affecting layout.
    Prop<float> offset_x = 0.f;
    Prop<float> offset_y = 0.f;
    // Stacking order among siblings, when set.
    std::optional<Prop<float>> z_index;
    // Hit testing: by default a view is hit where its content draws (shapes by their outline,
    // text by its frame), within any clip.
    std::optional<Shape> content_shape; // hit test this shape (filling the bounds) instead
    bool allows_hit_testing = true;
};

// Changes how the modified view's content is drawn and hit tested, not its layout.
class ELG_API EffectModifier : public Modifier {
public:
    ModifierProtocol protocol() const noexcept final { return ModifierProtocol::effect; }
    virtual void apply(Effects& effects) const = 0;
};

// Handles input for the modified view: makes one EventHandler per mounted view (design.md §10).
class ELG_API EventModifier : public Modifier {
public:
    ModifierProtocol protocol() const noexcept final { return ModifierProtocol::event; }
    virtual std::unique_ptr<EventHandler> make_handler() const = 0;
};

} // namespace elg

#endif // ELG_MODIFIER_H
