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

class ELG_API BackgroundModifier final : public DecorationModifier {
public:
    explicit BackgroundModifier(View view, Alignment alignment = Alignment::center)
        : view_(std::move(view)), alignment_(alignment) {}

    View decoration() const override { return view_; }
    Placement placement() const override { return Placement::behind; }
    Alignment alignment() const override { return alignment_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    View view_;
    Alignment alignment_;
    ELG_PRIVATE_MEMBERS_END
};

class ELG_API OverlayModifier final : public DecorationModifier {
public:
    explicit OverlayModifier(View view, Alignment alignment = Alignment::center)
        : view_(std::move(view)), alignment_(alignment) {}

    View decoration() const override { return view_; }
    Placement placement() const override { return Placement::in_front; }
    Alignment alignment() const override { return alignment_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    View view_;
    Alignment alignment_;
    ELG_PRIVATE_MEMBERS_END
};

// Composition only: an overlay of Rectangle().stroke_border(color, width).
// Shows the content only where the mask view is opaque (View::mask).
class ELG_API MaskModifier final : public DecorationModifier {
public:
    explicit MaskModifier(View view, Alignment alignment = Alignment::center)
        : view_(std::move(view)), alignment_(alignment) {}

    View decoration() const override { return view_; }
    Placement placement() const override { return Placement::mask; }
    Alignment alignment() const override { return alignment_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    View view_;
    Alignment alignment_;
    ELG_PRIVATE_MEMBERS_END
};

} // namespace elg

#endif // ELG_MODIFIERS_DECORATION_H
