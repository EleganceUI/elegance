// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// VStack, HStack, and ZStack (design.md §7).

#include <elg/layout.h>

#include <algorithm>
#include <numeric>
#include <vector>

namespace elg {

namespace {

float main_of(Size size, Axis axis) {
    return axis == Axis::horizontal ? size.width : size.height;
}

float cross_of(Size size, Axis axis) {
    return axis == Axis::horizontal ? size.height : size.width;
}

std::optional<float> main_of(ProposedSize proposal, Axis axis) {
    return axis == Axis::horizontal ? proposal.width : proposal.height;
}

std::optional<float> cross_of(ProposedSize proposal, Axis axis) {
    return axis == Axis::horizontal ? proposal.height : proposal.width;
}

ProposedSize make_proposal(Axis axis, std::optional<float> main, std::optional<float> cross) {
    return axis == Axis::horizontal ? ProposedSize{main, cross} : ProposedSize{cross, main};
}

float fraction(HorizontalAlignment alignment) {
    switch (alignment) {
    case HorizontalAlignment::leading: return 0;
    case HorizontalAlignment::center: return 0.5f;
    case HorizontalAlignment::trailing: return 1;
    }
    return 0.5f;
}

float fraction(VerticalAlignment alignment) {
    switch (alignment) {
    case VerticalAlignment::top: return 0;
    case VerticalAlignment::center: return 0.5f;
    case VerticalAlignment::bottom: return 1;
    }
    return 0.5f;
}

// Gaps between consecutive children. The default spacing is skipped next to a Spacer.
std::vector<float> gaps(std::span<const LayoutChild> children, std::optional<float> spacing) {
    std::vector<float> result;
    for (std::size_t i = 1; i < children.size(); ++i) {
        const bool next_to_spacer = children[i - 1].is_spacer() || children[i].is_spacer();
        result.push_back(spacing.value_or(next_to_spacer ? 0.f : default_stack_spacing));
    }
    return result;
}

// Splits the main axis among children, SwiftUI-style: higher layout priorities first; within a
// priority, non-spacers before spacers, then the least flexible children (smallest max - min
// range) first, each offered an equal share of what's left. Lower-priority children keep their
// minimum sizes reserved.
std::vector<float> distribute(Axis axis, ProposedSize proposal, std::span<const LayoutChild> children,
                              float total_gap) {
    const std::size_t n = children.size();
    std::vector<float> sizes(n, 0.f);
    const std::optional<float> cross = cross_of(proposal, axis);
    const std::optional<float> main = main_of(proposal, axis);

    if (!main) {
        for (std::size_t i = 0; i < n; ++i) {
            sizes[i] = main_of(children[i].size_that_fits(make_proposal(axis, std::nullopt, cross)), axis);
        }
        return sizes;
    }

    struct Candidate {
        std::size_t index;
        float priority;
        bool spacer;
        float min;
        float flexibility;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(n);
    float reserved = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const float min = main_of(children[i].size_that_fits(make_proposal(axis, 0.f, cross)), axis);
        const float max = main_of(children[i].size_that_fits(make_proposal(axis, infinity, cross)), axis);
        candidates.push_back({i, children[i].layout_priority(), children[i].is_spacer(), min, max - min});
        reserved += min;
    }
    // Within a priority, spacers go last: they only take what the content leaves, so
    // HStack { Text(...); Spacer() } shows the text in full when it fits.
    std::ranges::stable_sort(candidates, [](const Candidate& a, const Candidate& b) {
        if (a.priority != b.priority) {
            return a.priority > b.priority;
        }
        if (a.spacer != b.spacer) {
            return !a.spacer;
        }
        return a.flexibility < b.flexibility;
    });

    float remaining = *main - total_gap - reserved;
    for (std::size_t group = 0; group < n;) {
        std::size_t end = group;
        std::size_t content_left = 0;
        std::size_t spacers_left = 0;
        float spacer_reserve = 0; // spacers' minimum lengths, kept out of the content's shares
        while (end < n && candidates[end].priority == candidates[group].priority) {
            remaining += candidates[end].min; // this group's reservation is now up for grabs
            if (candidates[end].spacer) {
                ++spacers_left;
                spacer_reserve += candidates[end].min;
            } else {
                ++content_left;
            }
            ++end;
        }
        // Content first (it's sorted before spacers), splitting what's left among itself; then
        // spacers split the rest.
        for (std::size_t k = group; k < end; ++k) {
            const Candidate& candidate = candidates[k];
            float share = 0;
            if (!candidate.spacer) {
                share = (remaining - spacer_reserve) / static_cast<float>(content_left--);
            } else {
                share = remaining / static_cast<float>(spacers_left--);
                spacer_reserve -= candidate.min;
            }
            const std::size_t index = candidate.index;
            sizes[index] = main_of(children[index].size_that_fits(make_proposal(axis, std::max(0.f, share), cross)), axis);
            remaining -= sizes[index];
        }
        group = end;
    }
    return sizes;
}

Size stack_size(Axis axis, std::optional<float> spacing, ProposedSize proposal,
                std::span<const LayoutChild> children) {
    if (children.empty()) {
        return {};
    }
    const std::vector<float> gap = gaps(children, spacing);
    const float total_gap = std::accumulate(gap.begin(), gap.end(), 0.f);
    const std::vector<float> sizes = distribute(axis, proposal, children, total_gap);

    float main = total_gap;
    float cross = 0;
    for (std::size_t i = 0; i < children.size(); ++i) {
        const Size size = children[i].size_that_fits(make_proposal(axis, sizes[i], cross_of(proposal, axis)));
        main += main_of(size, axis);
        cross = std::max(cross, cross_of(size, axis));
    }
    return axis == Axis::horizontal ? Size{main, cross} : Size{cross, main};
}

void stack_place(Axis axis, std::optional<float> spacing, float cross_fraction, Rect bounds,
                 ProposedSize proposal, std::span<const LayoutChild> children) {
    if (children.empty()) {
        return;
    }
    const std::vector<float> gap = gaps(children, spacing);
    const float total_gap = std::accumulate(gap.begin(), gap.end(), 0.f);
    const std::vector<float> sizes = distribute(axis, proposal, children, total_gap);

    const bool horizontal = axis == Axis::horizontal;
    const float cross_origin = horizontal ? bounds.y : bounds.x;
    const float cross_extent = horizontal ? bounds.height : bounds.width;
    float cursor = horizontal ? bounds.x : bounds.y;

    for (std::size_t i = 0; i < children.size(); ++i) {
        const ProposedSize child_proposal = make_proposal(axis, sizes[i], cross_of(proposal, axis));
        const Size size = children[i].size_that_fits(child_proposal);
        const float cross = cross_origin + (cross_extent - cross_of(size, axis)) * cross_fraction;
        children[i].place(horizontal ? Point{cursor, cross} : Point{cross, cursor}, child_proposal);
        cursor += main_of(size, axis) + (i < gap.size() ? gap[i] : 0.f);
    }
}

} // namespace

Point aligned_position(Rect container, Size size, Alignment alignment) noexcept {
    return {container.x + (container.width - size.width) * fraction(alignment.horizontal),
            container.y + (container.height - size.height) * fraction(alignment.vertical)};
}

Size VStackLayout::size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const {
    return stack_size(Axis::vertical, spacing_, proposal, children);
}

void VStackLayout::place_children(Rect bounds, ProposedSize proposal,
                                  std::span<const LayoutChild> children) const {
    stack_place(Axis::vertical, spacing_, fraction(alignment_), bounds, proposal, children);
}

Size HStackLayout::size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const {
    return stack_size(Axis::horizontal, spacing_, proposal, children);
}

void HStackLayout::place_children(Rect bounds, ProposedSize proposal,
                                  std::span<const LayoutChild> children) const {
    stack_place(Axis::horizontal, spacing_, fraction(alignment_), bounds, proposal, children);
}

Size ZStackLayout::size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const {
    Size result;
    for (const LayoutChild& child : children) {
        const Size size = child.size_that_fits(proposal);
        result.width = std::max(result.width, size.width);
        result.height = std::max(result.height, size.height);
    }
    return result;
}

void ZStackLayout::place_children(Rect bounds, ProposedSize proposal,
                                  std::span<const LayoutChild> children) const {
    for (const LayoutChild& child : children) {
        child.place(aligned_position(bounds, child.size_that_fits(proposal), alignment_), proposal);
    }
}

} // namespace elg
