// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_LAYOUT_H
#define ELG_LAYOUT_H

// Container layouts (design.md §7). The built-in stacks implement the same public protocol
// that custom layouts use.

#include <elg/export.h>
#include <elg/modifier.h>
#include <elg/view.h>

#include <concepts>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace elg {

/// Where a layout keeps its cache: one per laid-out view, owned by the framework.
struct LayoutCache {
    std::shared_ptr<void> value; ///< The cache; null until the view is first laid out.
    /// Set when the children change or one of them may have a new size.
    bool stale = true;
};

/// Arranges any number of children:
///
/// @code
/// struct FlowLayout : elg::Layout { ... };
/// FlowLayout{}(child1, child2, child3)    // a view, used like a stack
/// @endcode
///
/// For layouts that compute something from their children once per change rather than on every
/// measure (row breaks, column widths), derive from CachedLayout<Cache> instead.
class ELG_API Layout {
public:
    virtual ~Layout();

    /// Measures the layout: the size it takes for `proposal`. May be called several times per
    /// frame with different proposals, and measure children as often as it needs.
    ///
    /// @param proposal The size the parent offers: per dimension, nullopt asks for the ideal size, 0 for
    ///   the minimum and infinity for the maximum.
    /// @param children The children, in order.
    /// @return The layout's size.
    [[nodiscard]] virtual Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const = 0;
    /// Places every child, with LayoutChild::place().
    ///
    /// @param bounds Has the size reported for `proposal`, positioned by the parent.
    /// @param proposal The proposal that size was measured for.
    /// @param children The children, in order.
    virtual void place_children(Rect bounds, ProposedSize proposal,
                                std::span<const LayoutChild> children) const = 0;
    /// The axis along which Spacer children expand. Stacks return their axis.
    ///
    /// @return The axis, or std::nullopt (the default) for none.
    [[nodiscard]] virtual std::optional<Axis> spacer_axis() const { return std::nullopt; }

    /// What the framework calls, with the view's cache storage. CachedLayout implements these;
    /// other layouts ignore the cache.
    ///
    /// @param proposal The size the parent offers: per dimension, nullopt asks for the ideal size, 0 for
    ///   the minimum and infinity for the maximum.
    /// @param children The children, in order.
    /// @return The layout's size, from size_that_fits().
    virtual Size cached_size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children,
                                       LayoutCache&) const {
        return size_that_fits(proposal, children);
    }
    /// Like cached_size_that_fits(), but for place_children().
    ///
    /// @param bounds Has the size reported for `proposal`, positioned by the parent.
    /// @param proposal The proposal that size was measured for.
    /// @param children The children, in order.
    virtual void cached_place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children,
                                       LayoutCache&) const {
        place_children(bounds, proposal, children);
    }

    /// A view that arranges `children` with a copy of this layout.
    ///
    /// @param self The layout, copied as its own type.
    /// @param children The child views, in order.
    /// @return The view.
    template <class Self, std::convertible_to<View>... Children>
    View operator()(this const Self& self, Children&&... children) {
        return make_view(std::make_shared<const Self>(self), {View(std::forward<Children>(children))...});
    }

    /// Like operator()(Children&&...), but with the children in a vector, e.g. built in a loop.
    ///
    /// @param self The layout, copied as its own type.
    /// @param children The child views, in order.
    /// @return The view.
    template <class Self>
    View operator()(this const Self& self, std::vector<View> children) {
        return make_view(std::make_shared<const Self>(self), std::move(children));
    }

protected:
    Layout() = default;
    Layout(const Layout&) = default;
    Layout(Layout&&) = default;
    Layout& operator=(const Layout&) = default;
    Layout& operator=(Layout&&) = default;

private:
    static View make_view(std::shared_ptr<const Layout> layout, std::vector<View> children);
};

/// A layout with a cache (SwiftUI's makeCache / updateCache): computed from the children when
/// the view is first laid out, refreshed when its children change or one of them may have a new
/// size, and kept between measuring and placing, and between frames.
///
/// @code
/// struct FlowCache { std::vector<Size> sizes; };
/// struct FlowLayout : elg::CachedLayout<FlowCache> {
///     FlowCache make_cache(std::span<const LayoutChild> children) const override;
///     Size size_that_fits(ProposedSize, std::span<const LayoutChild>, FlowCache&) const override;
///     void place_children(Rect, ProposedSize, std::span<const LayoutChild>, FlowCache&) const override;
/// };
/// @endcode
///
/// The cache must not depend on the proposal (it isn't refreshed when only the proposal changes).
///
/// @tparam Cache What the layout computes from its children and keeps between calls.
template <class Cache>
class CachedLayout : public Layout {
public:
    /// Computes the cache from the children, when the view is first laid out.
    ///
    /// @param children The children, in order.
    /// @return The new cache.
    [[nodiscard]] virtual Cache make_cache(std::span<const LayoutChild> children) const = 0;
    /// Refreshes `cache` after a change; by default, makes a new one.
    ///
    /// @param cache The cache to update in place.
    /// @param children The children, in order.
    virtual void update_cache(Cache& cache, std::span<const LayoutChild> children) const {
        cache = make_cache(children);
    }
    /// Like Layout::size_that_fits(), with the cache.
    ///
    /// @param proposal The size the parent offers: per dimension, nullopt asks for the ideal size, 0 for
    ///   the minimum and infinity for the maximum.
    /// @param children The children, in order.
    /// @param cache The view's cache, up to date.
    /// @return The layout's size.
    virtual Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children, Cache& cache) const = 0;
    /// Like Layout::place_children(), with the cache.
    ///
    /// @param bounds Has the size reported for `proposal`, positioned by the parent.
    /// @param proposal The proposal that size was measured for.
    /// @param children The children, in order.
    /// @param cache The view's cache, up to date.
    virtual void place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children,
                                Cache& cache) const = 0;

    /// Without the framework's storage (e.g. called by another layout): a fresh cache each time.
    ///
    /// @param proposal The size the parent offers: per dimension, nullopt asks for the ideal size, 0 for
    ///   the minimum and infinity for the maximum.
    /// @param children The children, in order.
    /// @return The layout's size.
    [[nodiscard]] Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const final {
        Cache cache = make_cache(children);
        return size_that_fits(proposal, children, cache);
    }
    /// Without the framework's storage (e.g. called by another layout): a fresh cache each time.
    ///
    /// @param bounds Has the size reported for `proposal`, positioned by the parent.
    /// @param proposal The proposal that size was measured for.
    /// @param children The children, in order.
    void place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children) const final {
        Cache cache = make_cache(children);
        place_children(bounds, proposal, children, cache);
    }
    /// Measures with the cache in `storage`, making it on first use and updating it when stale.
    ///
    /// @param proposal The size the parent offers: per dimension, nullopt asks for the ideal size, 0 for
    ///   the minimum and infinity for the maximum.
    /// @param children The children, in order.
    /// @param storage The view's cache storage.
    /// @return The layout's size.
    Size cached_size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children,
                               LayoutCache& storage) const final {
        return size_that_fits(proposal, children, cache_in(storage, children));
    }
    /// Places with the cache in `storage`, making it on first use and updating it when stale.
    ///
    /// @param bounds Has the size reported for `proposal`, positioned by the parent.
    /// @param proposal The proposal that size was measured for.
    /// @param children The children, in order.
    /// @param storage The view's cache storage.
    void cached_place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children,
                               LayoutCache& storage) const final {
        place_children(bounds, proposal, children, cache_in(storage, children));
    }

private:
    Cache& cache_in(LayoutCache& storage, std::span<const LayoutChild> children) const {
        if (!storage.value) {
            storage.value = std::make_shared<Cache>(make_cache(children));
        } else if (storage.stale) {
            update_cache(*static_cast<Cache*>(storage.value.get()), children);
        }
        storage.stale = false;
        return *static_cast<Cache*>(storage.value.get());
    }
};

/// Default gap between stack children; none next to a Spacer.
inline constexpr float default_stack_spacing = 8;

/// VStack's layout: children top to bottom. The height is split SwiftUI-style: higher layout
/// priorities first; within a priority, other children before Spacers, then the least flexible
/// first, each offered an equal share of what's left.
class ELG_API VStackLayout final : public Layout {
public:
    /// @param alignment Where narrower children go across the stack.
    /// @param spacing The gap between children in points; unset: default_stack_spacing, and none
    ///   next to a Spacer.
    explicit VStackLayout(HorizontalAlignment alignment = HorizontalAlignment::center,
                          OptionalLength spacing = {}) noexcept
        : alignment_(alignment), spacing_(spacing) {}

    /// Measures the stack: the children's heights and gaps added up, by the widest child.
    ///
    /// @param proposal The size the parent offers: per dimension, nullopt asks for the ideal size, 0 for
    ///   the minimum and infinity for the maximum.
    /// @param children The children, top to bottom.
    /// @return The stack's size; zero without children.
    [[nodiscard]] Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override;
    /// Places the children top to bottom from the top of `bounds`.
    ///
    /// @param bounds Has the size reported for `proposal`, positioned by the parent.
    /// @param proposal The proposal that size was measured for.
    /// @param children The children, top to bottom.
    void place_children(Rect bounds, ProposedSize proposal,
                        std::span<const LayoutChild> children) const override;
    /// @return Axis::vertical: Spacers grow vertically.
    [[nodiscard]] std::optional<Axis> spacer_axis() const override { return Axis::vertical; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    HorizontalAlignment alignment_;
    std::optional<float> spacing_;
    ELG_PRIVATE_MEMBERS_END
};

/// HStack's layout: children left to right, the width split like VStackLayout splits the height.
class ELG_API HStackLayout final : public Layout {
public:
    /// @param alignment Where shorter children go across the stack.
    /// @param spacing The gap between children in points; unset: default_stack_spacing, and none
    ///   next to a Spacer.
    explicit HStackLayout(VerticalAlignment alignment = VerticalAlignment::center,
                          OptionalLength spacing = {}) noexcept
        : alignment_(alignment), spacing_(spacing) {}

    /// Measures the stack: the children's widths and gaps added up, by the tallest child.
    ///
    /// @param proposal The size the parent offers: per dimension, nullopt asks for the ideal size, 0 for
    ///   the minimum and infinity for the maximum.
    /// @param children The children, left to right.
    /// @return The stack's size; zero without children.
    [[nodiscard]] Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override;
    /// Places the children left to right from the left of `bounds`.
    ///
    /// @param bounds Has the size reported for `proposal`, positioned by the parent.
    /// @param proposal The proposal that size was measured for.
    /// @param children The children, left to right.
    void place_children(Rect bounds, ProposedSize proposal,
                        std::span<const LayoutChild> children) const override;
    /// @return Axis::horizontal: Spacers grow horizontally.
    [[nodiscard]] std::optional<Axis> spacer_axis() const override { return Axis::horizontal; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    VerticalAlignment alignment_;
    std::optional<float> spacing_;
    ELG_PRIVATE_MEMBERS_END
};

/// ZStack's layout: children on top of each other, later ones in front. Like SwiftUI, the children
/// with the highest layout priority size the stack; the rest are offered that size.
class ELG_API ZStackLayout final : public Layout {
public:
    /// @param alignment Where each child goes within the stack.
    explicit ZStackLayout(Alignment alignment = Alignment::center) noexcept : alignment_(alignment) {}

    /// Measures the stack: the union of its children's sizes.
    ///
    /// @param proposal The size the parent offers: per dimension, nullopt asks for the ideal size, 0 for
    ///   the minimum and infinity for the maximum.
    /// @param children The children, back to front.
    /// @return The stack's size; zero without children.
    [[nodiscard]] Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override;
    /// Places each child within `bounds` by the stack's alignment.
    ///
    /// @param bounds Has the size reported for `proposal`, positioned by the parent.
    /// @param proposal The proposal that size was measured for.
    /// @param children The children, back to front.
    void place_children(Rect bounds, ProposedSize proposal,
                        std::span<const LayoutChild> children) const override;

private:
    Alignment alignment_;
};

/// Where a child of `size` goes inside `container` for `alignment`.
///
/// @param container The area to align in.
/// @param size The child's size; may be larger than `container`, which puts it partly outside.
/// @param alignment Where in `container` the child goes.
/// @return The child's top-left corner.
ELG_API Point aligned_position(Rect container, Size size, Alignment alignment) noexcept;

} // namespace elg

#endif // ELG_LAYOUT_H
