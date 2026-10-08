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

// Arranges any number of children:
//   struct FlowLayout : elg::Layout { ... };
//   FlowLayout{}(child1, child2, child3)    // a view, used like a stack
class ELG_API Layout {
public:
    virtual ~Layout();

    virtual Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const = 0;
    // `bounds` has the size reported for `proposal`, positioned by the parent.
    virtual void place_children(Rect bounds, ProposedSize proposal,
                                std::span<const LayoutChild> children) const = 0;
    // The axis along which Spacer children expand. Stacks return their axis.
    virtual std::optional<Axis> spacer_axis() const { return std::nullopt; }

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
