---
title: Custom layouts
description: "Writing your own container layouts with Layout and CachedLayout, and passing values from children to their layout."
---

The built-in stacks implement the same public protocol you can use for your own containers: a
`Layout` measures its children and places them.

## A layout

Derive from `Layout` and implement two functions:

- `size_that_fits(proposal, children)` returns the size the container wants for a proposal. Measure
  children with `child.size_that_fits(...)`, as often as you need.
- `place_children(bounds, proposal, children)` positions each child with `child.place(position,
  proposal)`. `bounds` has the size you reported, positioned by your parent.

Here's a layout that wraps its children into rows, like words in a paragraph:

```cpp
class FlowLayout : public Layout {
public:
    explicit FlowLayout(float spacing = 8) : spacing_(spacing) {}

    Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children) const override {
        return arrange(proposal.width.value_or(infinity), children, nullptr);
    }

    void place_children(Rect bounds, ProposedSize, std::span<const LayoutChild> children) const override {
        arrange(bounds.width, children, &bounds);
    }

private:
    Size arrange(float max_width, std::span<const LayoutChild> children, const Rect* bounds) const {
        float x = 0, y = 0, row_height = 0, width = 0;
        for (const LayoutChild& child : children) {
            const Size size = child.size_that_fits(ProposedSize::unspecified()); // its ideal size
            if (x > 0 && x + size.width > max_width) {
                x = 0;
                y += row_height + spacing_;
                row_height = 0;
            }
            if (bounds) {
                child.place({bounds->x + x, bounds->y + y}, ProposedSize{size.width, size.height});
            }
            x += size.width + spacing_;
            width = std::max(width, x - spacing_);
            row_height = std::max(row_height, size.height);
        }
        return {width, y + row_height};
    }

    float spacing_;
};
```

Use it like a stack, with children as arguments or as a vector:

```cpp
FlowLayout(6)(Tag("C++"), Tag("Vulkan"), Tag("UI"));
FlowLayout()(std::move(tag_views)); // std::vector<View>
```

The [layout example](../../examples/#layout) uses this layout for its tags.

### Proposals

A `ProposedSize` has an optional width and height. Unset asks a child for its ideal size, zero for
its minimum, and `infinity` for its maximum. Proposing `ProposedSize::unspecified()` and reading
the result is the usual way to find a child's natural size.

`spacer_axis()` tells `Spacer` children which way to expand; stacks return their axis.

## Caching

`size_that_fits` can be called several times per layout pass. When a layout computes something
expensive from its children that doesn't depend on the proposal (their ideal sizes, for example),
derive from `CachedLayout<Cache>` instead:

```cpp
struct FlowCache {
    std::vector<Size> sizes;
};

class CachedFlow : public CachedLayout<FlowCache> {
public:
    FlowCache make_cache(std::span<const LayoutChild> children) const override {
        FlowCache cache;
        for (const LayoutChild& child : children) {
            cache.sizes.push_back(child.size_that_fits(ProposedSize::unspecified()));
        }
        return cache;
    }

    Size size_that_fits(ProposedSize proposal, std::span<const LayoutChild> children, FlowCache& cache) const override;
    void place_children(Rect bounds, ProposedSize proposal, std::span<const LayoutChild> children,
                        FlowCache& cache) const override;
};
```

The cache is made when the view is first laid out, refreshed (with `update_cache`, which makes a
new one by default) when the children change or one of them may have a new size, and kept between
measuring and placing and between frames.

## Values from children

Children can carry values for their layout with `.layout_value<Key>(value)`. A key is a type with
a `Value` type and a `default_value()`:

```cpp
struct Column {
    using Value = int;
    static int default_value() { return 0; }
};

GridLayout()(Text("Name").layout_value<Column>(0), Text("Email").layout_value<Column>(1));
```

The layout reads it with `child.value<Column>()`. `layout_priority()` and `is_spacer()` are
available on every child too.
