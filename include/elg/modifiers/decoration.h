// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MODIFIERS_DECORATION_H
#define ELG_MODIFIERS_DECORATION_H

// Built-in decoration modifiers. View::background() and overlay() are sugar over these; border()
// is an overlay of a stroked shape.

#include <elg/color.h>
#include <elg/export.h>
#include <elg/modifier.h>
#include <elg/view.h>

#include <utility>

namespace elg {

/// Draws a view behind the content, offered exactly the content's size (View::background()).
class ELG_API BackgroundModifier final : public DecorationModifier {
public:
    /// `view` behind the content.
    ///
    /// @param view The background.
    /// @param alignment Where `view` sits when its size differs from the content's.
    explicit BackgroundModifier(View view, Alignment alignment = Alignment::center)
        : view_(std::move(view)), alignment_(alignment) {}

    /// @return The background.
    [[nodiscard]] View decoration() const override { return view_; }
    /// @return Placement::behind.
    [[nodiscard]] Placement placement() const override { return Placement::behind; }
    /// @return Where the background sits when its size differs from the content's.
    [[nodiscard]] Alignment alignment() const override { return alignment_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    View view_;
    Alignment alignment_;
    ELG_PRIVATE_MEMBERS_END
};

/// Draws a view in front of the content, offered exactly the content's size (View::overlay()).
class ELG_API OverlayModifier final : public DecorationModifier {
public:
    /// `view` in front of the content.
    ///
    /// @param view The overlay.
    /// @param alignment Where `view` sits when its size differs from the content's.
    explicit OverlayModifier(View view, Alignment alignment = Alignment::center)
        : view_(std::move(view)), alignment_(alignment) {}

    /// @return The overlay.
    [[nodiscard]] View decoration() const override { return view_; }
    /// @return Placement::in_front.
    [[nodiscard]] Placement placement() const override { return Placement::in_front; }
    /// @return Where the overlay sits when its size differs from the content's.
    [[nodiscard]] Alignment alignment() const override { return alignment_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    View view_;
    Alignment alignment_;
    ELG_PRIVATE_MEMBERS_END
};

/// Shows the content only where the mask view is opaque (View::mask).
class ELG_API MaskModifier final : public DecorationModifier {
public:
    /// The content, masked by `view`.
    ///
    /// @param view The mask; only its alpha matters.
    /// @param alignment Where `view` sits when its size differs from the content's.
    explicit MaskModifier(View view, Alignment alignment = Alignment::center)
        : view_(std::move(view)), alignment_(alignment) {}

    /// @return The mask.
    [[nodiscard]] View decoration() const override { return view_; }
    /// @return Placement::mask.
    [[nodiscard]] Placement placement() const override { return Placement::mask; }
    /// @return Where the mask sits when its size differs from the content's.
    [[nodiscard]] Alignment alignment() const override { return alignment_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    View view_;
    Alignment alignment_;
    ELG_PRIVATE_MEMBERS_END
};

} // namespace elg

#endif // ELG_MODIFIERS_DECORATION_H
