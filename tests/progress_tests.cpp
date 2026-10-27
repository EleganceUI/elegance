// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Progress: a fraction or activity, whatever its style draws.

#include "text/text_system.h"
#include "view/view_node.h"

#include <elg/styles.h>
#include <elg/views.h>

#include <catch2/catch_test_macros.hpp>

#include <optional>

using namespace elg;

namespace {

struct Mounted {
    std::unique_ptr<detail::Element> root;

    // A custom style's Text needs a text system.
    explicit Mounted(const View& view) : root(detail::lower(view, {.text = &text_system()})) {}

    static detail::TextSystem& text_system() {
        static detail::TextSystem system;
        return system;
    }

    detail::AccessibilityInfo info() const {
        detail::AccessibilityInfo out;
        root->accessibility(out);
        return out;
    }
    Size size(ProposedSize proposal) const { return root->size_that_fits(proposal); }
    // Lays it out in `proposal` and draws it.
    detail::DisplayList paint(ProposedSize proposal) const {
        root->size_that_fits(proposal);
        root->place({}, proposal);
        detail::DisplayList list;
        root->paint(list);
        return list;
    }
};

} // namespace

TEST_CASE("progress reads as a progress indicator with its fraction", "[progress]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<float> done(0.25f);
    const Mounted progress(Progress(done));
    CHECK(progress.info().role == detail::AccessRole::progress);
    CHECK(progress.info().numeric == 0.25);
    CHECK(progress.info().minimum == 0);
    CHECK(progress.info().maximum == 1);
    done = 3.f; // clamped
    detail::flush_effects();
    CHECK(progress.info().numeric == 1);
}

TEST_CASE("progress without a fraction is indeterminate, and can become determinate", "[progress]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<std::optional<float>> fraction;
    const Mounted progress(Progress(fraction));
    CHECK(progress.info().role == detail::AccessRole::progress);
    CHECK_FALSE(progress.info().numeric);
    fraction = 0.5f;
    detail::flush_effects();
    CHECK(progress.info().numeric == 0.5);

    const Mounted plain(Progress());
    CHECK_FALSE(plain.info().numeric);
}

TEST_CASE("the bar fills the width offered and the ring keeps its size", "[progress]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    const Mounted bar(Progress(0.4f).progress_style(LinearProgressStyle{{.fill = ThemeColor::success, .thickness = 3}}));
    const Size bar_size = bar.size({200.f, 50.f});
    CHECK(bar_size.width == 200);
    CHECK(bar_size.height == 3);
    CHECK(bar.paint({200.f, 50.f}).commands.size() == 2); // track and fill

    const Mounted ring(Progress().progress_style(CircularProgressStyle{{.size = 14, .line_width = 2}}));
    const Size ring_size = ring.size({200.f, 50.f});
    CHECK(ring_size.width == 14);
    CHECK(ring_size.height == 14);
    CHECK(ring.info().role == detail::AccessRole::progress);
    const detail::DisplayList spinning = ring.paint({200.f, 50.f});
    CHECK(spinning.commands.size() == 2); // track and arc
    CHECK(spinning.animating);
}

namespace {

// A custom style sees the fraction.
class TextProgressStyle final : public ProgressStyle {
public:
    View make_body(const ProgressConfiguration& progress) const override {
        return Text([fraction = progress.fraction] {
            const std::optional<float> f = fraction();
            return f ? std::to_string(static_cast<int>(*f * 100)) + "%" : std::string("…");
        });
    }
};

} // namespace

TEST_CASE("a custom progress style gets the fraction", "[progress]") {
    detail::Scope scope;
    detail::Scope::Use use(scope);
    State<double> done(0.5);
    const Mounted progress(Progress([=] { return static_cast<float>(done()); }).progress_style(TextProgressStyle{}));
    CHECK(progress.info().role == detail::AccessRole::progress);
    CHECK(progress.info().numeric == 0.5);
}
