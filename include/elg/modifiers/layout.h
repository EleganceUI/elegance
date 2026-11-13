// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MODIFIERS_LAYOUT_H
#define ELG_MODIFIERS_LAYOUT_H

// Built-in layout modifiers. View::padding(), frame(), ... are sugar over these.

#include <elg/export.h>
#include <elg/modifier.h>
#include <elg/view.h>

namespace elg {

/// Space around the content (View::padding()): the content is offered the proposal minus the
/// insets (never below 0), and the view is the content's size plus the insets.
class ELG_API PaddingModifier final : public LayoutModifier {
public:
    /// Pads by `insets`.
    ///
    /// @param insets The space on each edge, in points.
    explicit PaddingModifier(EdgeInsets insets) noexcept : insets_(insets) {}

    /// Measures the content inside the insets.
    ///
    /// @param proposal The size the parent offers.
    /// @param content The padded view.
    /// @return The content's size for the inset proposal, plus the insets.
    [[nodiscard]] Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const override;
    /// Places the content inside the insets.
    ///
    /// @param bounds Where the parent put the padded view.
    /// @param proposal The size the parent offered.
    /// @param content The padded view.
    void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const override;

    /// @return The space on each edge, in points.
    [[nodiscard]] EdgeInsets insets() const noexcept { return insets_; }

private:
    EdgeInsets insets_;
};

/// A frame of fixed dimensions (View::frame(FrameOptions)): set dimensions are the frame's and
/// what the content is offered; unset ones follow the content. The content is aligned in it.
class ELG_API FrameModifier final : public LayoutModifier {
public:
    /// A frame with `options`.
    ///
    /// @param options The width, height and alignment.
    explicit FrameModifier(FrameOptions options) noexcept : options_(options) {}

    /// Measures the content with the frame's dimensions.
    ///
    /// @param proposal The size the parent offers; used on dimensions the frame doesn't set.
    /// @param content The framed view.
    /// @return The frame's dimensions, and the content's on the others.
    [[nodiscard]] Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const override;
    /// Places the content in the frame by its alignment.
    ///
    /// @param bounds Where the parent put the frame.
    /// @param proposal The size the parent offered.
    /// @param content The framed view.
    void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const override;

private:
    [[nodiscard]] ProposedSize content_proposal(ProposedSize proposal) const noexcept;

    FrameOptions options_;
};

/// A frame whose size is a range (View::frame(FlexibleFrameOptions)): the proposal clamped to
/// the bounds, a missing bound taken from the content. The content is offered the clamped
/// proposal (the ideal length when there's none) and aligned in it.
class ELG_API FlexibleFrameModifier final : public LayoutModifier {
public:
    /// A frame with `options`.
    ///
    /// @param options The bounds, ideal lengths and alignment.
    explicit FlexibleFrameModifier(FlexibleFrameOptions options) noexcept : options_(options) {}

    /// Measures the content with the clamped proposal.
    ///
    /// @param proposal The size the parent offers.
    /// @param content The framed view.
    /// @return The frame's size: the proposal (or ideal length, or the content's) clamped to the
    ///   bounds.
    [[nodiscard]] Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const override;
    /// Places the content in the frame by its alignment.
    ///
    /// @param bounds Where the parent put the frame.
    /// @param proposal The size the parent offered.
    /// @param content The framed view.
    void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const override;

private:
    [[nodiscard]] ProposedSize content_proposal(ProposedSize proposal) const noexcept;

    FlexibleFrameOptions options_;
};

/// Proposes nothing on the chosen axes, so the content takes its ideal size (View::fixed_size()).
class ELG_API FixedSizeModifier final : public LayoutModifier {
public:
    /// Fixes the content's size on the chosen axes.
    ///
    /// @param horizontal Whether the width is the content's ideal width.
    /// @param vertical Whether the height is the content's ideal height.
    FixedSizeModifier(bool horizontal, bool vertical) noexcept
        : horizontal_(horizontal), vertical_(vertical) {}

    /// Measures the content with no proposal on the fixed axes.
    ///
    /// @param proposal The size the parent offers; passed on for the other axes.
    /// @param content The modified view.
    /// @return The content's size.
    [[nodiscard]] Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const override;
    /// Places the content at the bounds' origin.
    ///
    /// @param bounds Where the parent put the view.
    /// @param proposal The size the parent offered; passed on for the other axes.
    /// @param content The modified view.
    void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const override;

private:
    [[nodiscard]] ProposedSize content_proposal(ProposedSize proposal) const noexcept;

    bool horizontal_;
    bool vertical_;
};

/// Proposes the content sizes at an aspect ratio (View::aspect_ratio()): the largest that fits
/// the proposal (ContentMode::fit) or the smallest that covers it (ContentMode::fill).
class ELG_API AspectRatioModifier final : public LayoutModifier {
public:
    /// Keeps `ratio`.
    ///
    /// @param ratio Width divided by height; nullopt (or not positive): the ratio of the content's
    ///   ideal size, and no change when that's 0 on an axis.
    /// @param mode Whether the content fits in the proposed size or covers it.
    AspectRatioModifier(std::optional<float> ratio, ContentMode mode) noexcept : ratio_(ratio), mode_(mode) {}

    /// Measures the content with a proposal at the ratio.
    ///
    /// @param proposal The size the parent offers.
    /// @param content The modified view.
    /// @return The content's size for that proposal.
    [[nodiscard]] Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const override;
    /// Places the content at the bounds' origin, proposing it the size at the ratio.
    ///
    /// @param bounds Where the parent put the view.
    /// @param proposal The size the parent offered.
    /// @param content The modified view.
    void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const override;

private:
    [[nodiscard]] ProposedSize content_proposal(ProposedSize proposal, const LayoutChild& content) const;

    ELG_PRIVATE_MEMBERS_BEGIN
    std::optional<float> ratio_;
    ContentMode mode_;
    ELG_PRIVATE_MEMBERS_END
};

/// Sets the view's layout priority (View::layout_priority()); layout is otherwise the content's.
class ELG_API LayoutPriorityModifier final : public LayoutModifier {
public:
    /// Gives the view `priority`.
    ///
    /// @param priority Stacks give space to higher priorities first.
    explicit LayoutPriorityModifier(float priority) noexcept : priority_(priority) {}

    /// The content's size.
    ///
    /// @param proposal The size the parent offers.
    /// @param content The modified view.
    /// @return The content's size for `proposal`.
    [[nodiscard]] Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const override;
    /// Places the content at the bounds' origin with the parent's proposal.
    ///
    /// @param bounds Where the parent put the view.
    /// @param proposal The size the parent offered.
    /// @param content The modified view.
    void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const override;
    /// @return The priority.
    [[nodiscard]] std::optional<float> layout_priority() const override { return priority_; }

private:
    float priority_;
};

} // namespace elg

#endif // ELG_MODIFIERS_LAYOUT_H
