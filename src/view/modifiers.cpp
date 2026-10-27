// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Built-in modifiers, implemented only with the public protocols (design.md §9.4, dogfooding).

#include <elg/layout.h>
#include <elg/modifiers/decoration.h>
#include <elg/modifiers/layout.h>
#include <elg/shapes.h>

#include <algorithm>
#include <cmath>

namespace elg {

namespace {

std::optional<float> shrink(std::optional<float> length, float amount) {
    if (!length) {
        return length;
    }
    return std::max(0.f, *length - amount);
}

ProposedSize inset(ProposedSize proposal, EdgeInsets insets) {
    return {shrink(proposal.width, insets.leading + insets.trailing),
            shrink(proposal.height, insets.top + insets.bottom)};
}

// The proposal a flexible frame passes on: clamped into [min, max]; the ideal if unspecified.
std::optional<float> clamp_proposal(std::optional<float> proposed, std::optional<float> min,
                                    std::optional<float> ideal, std::optional<float> max) {
    if (!proposed) {
        return ideal;
    }
    float length = *proposed;
    if (min) {
        length = std::max(length, *min);
    }
    if (max) {
        length = std::min(length, *max);
    }
    return length;
}

// A flexible frame's own length on one axis: the proposal (or ideal, or the content's length),
// clamped into [min, max]. A missing bound is taken from the content, so .frame({.max_width =
// infinity}) fills the proposal and .frame({.min_width = 100}) is at least 100.
float resolve_length(std::optional<float> proposed, std::optional<float> min, std::optional<float> ideal,
                     std::optional<float> max, float content) {
    const float target = proposed ? *proposed : ideal.value_or(content);
    if (!min && !max) {
        return proposed ? content : target;
    }
    const float low = min ? *min : std::min(content, target);
    const float high = max ? *max : std::max(content, low);
    return std::max(low, std::min(target, high));
}

} // namespace

// --- PaddingModifier ---

Size PaddingModifier::size_that_fits(ProposedSize proposal, const LayoutChild& content) const {
    const Size size = content.size_that_fits(inset(proposal, insets_));
    return {size.width + insets_.leading + insets_.trailing, size.height + insets_.top + insets_.bottom};
}

void PaddingModifier::place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const {
    content.place({bounds.x + insets_.leading, bounds.y + insets_.top}, inset(proposal, insets_));
}

// --- FrameModifier ---

ProposedSize FrameModifier::content_proposal(ProposedSize proposal) const noexcept {
    return {options_.width ? std::optional<float>(options_.width) : proposal.width,
            options_.height ? std::optional<float>(options_.height) : proposal.height};
}

Size FrameModifier::size_that_fits(ProposedSize proposal, const LayoutChild& content) const {
    const Size size = content.size_that_fits(content_proposal(proposal));
    return {options_.width.value_or(size.width), options_.height.value_or(size.height)};
}

void FrameModifier::place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const {
    const ProposedSize offer = content_proposal(proposal);
    content.place(aligned_position(bounds, content.size_that_fits(offer), options_.alignment), offer);
}

// --- FlexibleFrameModifier ---

ProposedSize FlexibleFrameModifier::content_proposal(ProposedSize proposal) const noexcept {
    const FlexibleFrameOptions& o = options_;
    return {clamp_proposal(proposal.width, o.min_width, o.ideal_width, o.max_width),
            clamp_proposal(proposal.height, o.min_height, o.ideal_height, o.max_height)};
}

Size FlexibleFrameModifier::size_that_fits(ProposedSize proposal, const LayoutChild& content) const {
    const FlexibleFrameOptions& o = options_;
    const Size size = content.size_that_fits(content_proposal(proposal));
    return {resolve_length(proposal.width, o.min_width, o.ideal_width, o.max_width, size.width),
            resolve_length(proposal.height, o.min_height, o.ideal_height, o.max_height, size.height)};
}

void FlexibleFrameModifier::place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const {
    const ProposedSize offer = content_proposal(proposal);
    content.place(aligned_position(bounds, content.size_that_fits(offer), options_.alignment), offer);
}

// --- FixedSizeModifier ---

ProposedSize FixedSizeModifier::content_proposal(ProposedSize proposal) const noexcept {
    return {horizontal_ ? std::nullopt : proposal.width, vertical_ ? std::nullopt : proposal.height};
}

Size FixedSizeModifier::size_that_fits(ProposedSize proposal, const LayoutChild& content) const {
    return content.size_that_fits(content_proposal(proposal));
}

void FixedSizeModifier::place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const {
    content.place({bounds.x, bounds.y}, content_proposal(proposal));
}

// --- AspectRatioModifier ---

ProposedSize AspectRatioModifier::content_proposal(ProposedSize proposal, const LayoutChild& content) const {
    float ratio = ratio_.value_or(0);
    if (ratio <= 0) {
        const Size ideal = content.size_that_fits({});
        if (ideal.width <= 0 || ideal.height <= 0) {
            return proposal; // no shape to keep
        }
        ratio = ideal.width / ideal.height;
    }
    const auto finite = [](std::optional<float> v) { return v && std::isfinite(*v) ? v : std::nullopt; };
    const std::optional<float> width = finite(proposal.width);
    const std::optional<float> height = finite(proposal.height);
    if (width && height) {
        const float w = mode_ == ContentMode::fit ? std::min(*width, *height * ratio) : std::max(*width, *height * ratio);
        return {w, w / ratio};
    }
    if (width) {
        return {*width, *width / ratio};
    }
    if (height) {
        return {*height * ratio, *height};
    }
    // Nothing to fit into: the content's ideal width, at the ratio.
    const Size ideal = content.size_that_fits({});
    return {ideal.width, ideal.width / ratio};
}

Size AspectRatioModifier::size_that_fits(ProposedSize proposal, const LayoutChild& content) const {
    return content.size_that_fits(content_proposal(proposal, content));
}

void AspectRatioModifier::place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const {
    content.place({bounds.x, bounds.y}, content_proposal(proposal, content));
}

// --- LayoutPriorityModifier ---

Size LayoutPriorityModifier::size_that_fits(ProposedSize proposal, const LayoutChild& content) const {
    return content.size_that_fits(proposal);
}

void LayoutPriorityModifier::place(Rect bounds, ProposedSize proposal, const LayoutChild& content) const {
    content.place({bounds.x, bounds.y}, proposal);
}

} // namespace elg
