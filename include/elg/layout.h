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

// Where a layout keeps its cache: one per laid-out view, owned by the framework. `stale` is set
// when the children change or one of them may have a new size.
struct LayoutCache {
    std::shared_ptr<void> value;
    bool stale = true;
};

// Arranges any number of children:
//   struct FlowLayout : elg::Layout { ... };
//   FlowLayout{}(child1, child2, child3)    // a view, used like a stack
// For layouts that compute something from their children once per change rather than on every
// measure (row breaks, column widths), derive from CachedLayout<Cache> instead.
class ELG_API Layout {
public:
    virtual ~Layout();

    virtual Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const = 0;
    // `bounds` has the size reported for `proposal`, positioned by the parent.
    virtual void place_children(Rect bounds, ProposedSize proposal,
                                std::span<const LayoutChild> children) const = 0;
    // The axis along which Spacer children expand. Stacks return their axis.
    virtual std::optional<Axis> spacer_axis() const { return std::nullopt; }

    // What the framework calls, with the view's cache storage. CachedLayout implements these;
    // other layouts ignore the cache.
    virtual Size cached_size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children,
                                       LayoutCache&) const {
        return size_that_fits(proposal, children);
    }
    virtual void cached_place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children,
                                       LayoutCache&) const {
        place_children(bounds, proposal, children);
    }

    template <class Self, std::convertible_to<View>... Children>
    View operator()(this const Self& self, Children&&... children) {
        return make_view(std::make_shared<const Self>(self), {View(std::forward<Children>(children))...});
    }

    template <class Self>
    View operator()(this const Self& self, std::vector<View> children) {
        return make_view(std::make_shared<const Self>(self), std::move(children));
    }

protected:
    Layout() = default;
    Layout(const Layout&) = default;
    Layout& operator=(const Layout&) = default;

private:
    static View make_view(std::shared_ptr<const Layout> layout, std::vector<View> children);
};

// A layout with a cache (SwiftUI's makeCache / updateCache): computed from the children when
// the view is first laid out, refreshed when its children change or one of them may have a new
// size, and kept between measuring and placing, and between frames.
//   struct FlowCache { std::vector<Size> sizes; };
//   struct FlowLayout : elg::CachedLayout<FlowCache> {
//       FlowCache make_cache(std::span<const LayoutChild> children) const override;
//       Size size_that_fits(ProposedSize, std::span<const LayoutChild>, FlowCache&) const override;
//       void place_children(Rect, ProposedSize, std::span<const LayoutChild>, FlowCache&) const override;
//   };
// The cache must not depend on the proposal (it isn't refreshed when only the proposal changes).
template <class Cache>
class CachedLayout : public Layout {
public:
    virtual Cache make_cache(std::span<const LayoutChild> children) const = 0;
    // Refreshes `cache` after a change; by default, makes a new one.
    virtual void update_cache(Cache& cache, std::span<const LayoutChild> children) const {
        cache = make_cache(children);
    }
    virtual Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children, Cache& cache) const = 0;
    virtual void place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children,
                                Cache& cache) const = 0;

    // Without the framework's storage (e.g. called by another layout): a fresh cache each time.
    Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const final {
        Cache cache = make_cache(children);
        return size_that_fits(proposal, children, cache);
    }
    void place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children) const final {
        Cache cache = make_cache(children);
        place_children(bounds, proposal, children, cache);
    }
    Size cached_size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children,
                               LayoutCache& storage) const final {
        return size_that_fits(proposal, children, cache_in(storage, children));
    }
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

// Default gap between stack children; none next to a Spacer.
inline constexpr float default_stack_spacing = 8;

class ELG_API VStackLayout final : public Layout {
public:
    explicit VStackLayout(HorizontalAlignment alignment = HorizontalAlignment::center,
                          OptionalLength spacing = {}) noexcept
        : alignment_(alignment), spacing_(spacing) {}

    Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override;
    void place_children(Rect bounds, ProposedSize proposal,
                        std::span<const LayoutChild> children) const override;
    std::optional<Axis> spacer_axis() const override { return Axis::vertical; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    HorizontalAlignment alignment_;
    std::optional<float> spacing_;
    ELG_PRIVATE_MEMBERS_END
};

class ELG_API HStackLayout final : public Layout {
public:
    explicit HStackLayout(VerticalAlignment alignment = VerticalAlignment::center,
                          OptionalLength spacing = {}) noexcept
        : alignment_(alignment), spacing_(spacing) {}

    Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override;
    void place_children(Rect bounds, ProposedSize proposal,
                        std::span<const LayoutChild> children) const override;
    std::optional<Axis> spacer_axis() const override { return Axis::horizontal; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    VerticalAlignment alignment_;
    std::optional<float> spacing_;
    ELG_PRIVATE_MEMBERS_END
};

class ELG_API ZStackLayout final : public Layout {
public:
    explicit ZStackLayout(Alignment alignment = Alignment::center) noexcept : alignment_(alignment) {}

    Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override;
    void place_children(Rect bounds, ProposedSize proposal,
                        std::span<const LayoutChild> children) const override;

private:
    Alignment alignment_;
};

// Where a child of `size` goes inside `container` for `alignment`.
ELG_API Point aligned_position(Rect container, Size size, Alignment alignment) noexcept;

} // namespace elg

#endif // ELG_LAYOUT_H
