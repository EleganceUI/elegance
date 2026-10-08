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

class ELG_API PaddingModifier final : public LayoutModifier {
public:
    explicit PaddingModifier(EdgeInsets insets) noexcept : insets_(insets) {}

    Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const override;
    void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const override;

    EdgeInsets insets() const noexcept { return insets_; }

private:
    EdgeInsets insets_;
};

class ELG_API FrameModifier final : public LayoutModifier {
public:
    explicit FrameModifier(FrameOptions options) noexcept : options_(options) {}

    Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const override;
    void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const override;

private:
    ProposedSize content_proposal(ProposedSize proposal) const noexcept;

    FrameOptions options_;
};

class ELG_API FlexibleFrameModifier final : public LayoutModifier {
public:
    explicit FlexibleFrameModifier(FlexibleFrameOptions options) noexcept : options_(options) {}

    Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const override;
    void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const override;

private:
    ProposedSize content_proposal(ProposedSize proposal) const noexcept;

    FlexibleFrameOptions options_;
};

class ELG_API FixedSizeModifier final : public LayoutModifier {
public:
    FixedSizeModifier(bool horizontal, bool vertical) noexcept
        : horizontal_(horizontal), vertical_(vertical) {}

    Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const override;
    void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const override;

private:
    ProposedSize content_proposal(ProposedSize proposal) const noexcept;

    bool horizontal_;
    bool vertical_;
};

class ELG_API LayoutPriorityModifier final : public LayoutModifier {
public:
    explicit LayoutPriorityModifier(float priority) noexcept : priority_(priority) {}

    Size size_that_fits(ProposedSize proposal, const LayoutChild& content) const override;
    void place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const override;
    std::optional<float> layout_priority() const override { return priority_; }

private:
    float priority_;
};

} // namespace elg

#endif // ELG_MODIFIERS_LAYOUT_H
