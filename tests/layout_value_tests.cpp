// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Values that views carry for their parent layout (View::layout_value), and how their keys are
// told apart.

#include "view/element.h"
#include "view/view_node.h"

#include <elg/layout.h>
#include <elg/modifier.h>
#include <elg/reactive.h>
#include <elg/shapes.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string_view>
#include <utility>
#include <vector>

using namespace elg;

namespace {

struct Rank {
    using Value = int;
    static constexpr std::string_view name = "test.rank";
    static int default_value() { return 0; }
};

// Rank as another module (a views module) would define it: another type, with its own copy of the
// name.
struct RankElsewhere {
    using Value = int;
    static constexpr char text[] = {'t', 'e', 's', 't', '.', 'r', 'a', 'n', 'k'};
    static constexpr std::string_view name{text, sizeof(text)};
    static int default_value() { return 0; }
};

struct Order {
    using Value = int;
    static constexpr std::string_view name = "test.order";
    static int default_value() { return -1; }
};

// Records what its children carry for RankElsewhere and Order each time it's measured.
struct ReadingLayout final : Layout {
    explicit ReadingLayout(std::shared_ptr<std::vector<std::pair<int, int>>> record) : seen(std::move(record)) {}
    [[nodiscard]] Size size_that_fits(ProposedSize, std::span<const LayoutChild> children) const override {
        seen->clear();
        for (const LayoutChild& child : children) {
            seen->emplace_back(child.value<RankElsewhere>(), child.value<Order>());
        }
        return {};
    }
    void place_children(Rect, ProposedSize, std::span<const LayoutChild>) const override {}

    std::shared_ptr<std::vector<std::pair<int, int>>> seen;
};

} // namespace

TEST_CASE("layout values are told apart by their keys' names, not their types", "[hot-reload]") {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    auto seen = std::make_shared<std::vector<std::pair<int, int>>>();
    const auto root = detail::lower(
        ReadingLayout(seen)(Rectangle().layout_value<Rank>(3), Rectangle().layout_value<Order>(7), Rectangle()), {});
    (void)root->size_that_fits({100.f, 100.f});
    CHECK(*seen == std::vector<std::pair<int, int>>{{3, -1}, {0, 7}, {0, -1}});
}
