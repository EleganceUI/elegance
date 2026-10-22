// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// NavigationStack (design.md §13.5): pages driven by a path, sliding in and out.

#include "view/view_node.h"
#include "view/window_drag.h"

#include <elg/animation.h>
#include <elg/navigation.h>
#include <elg/styles.h>
#include <elg/theme.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace elg::detail {

namespace {

// How far the page underneath moves while another slides over it, as a fraction of the width.
constexpr float kUnderneathShift = 0.3f;

// A page of the stack: its content, alive while its route is in the path. While covered by
// another page it can't take focus (Tab, clicks) and screen readers don't see it; hit testing
// and drawing skip it anyway.
class PageElement final : public Element {
public:
    explicit PageElement(std::unique_ptr<Element> content) : content_(std::move(content)) { adopt(*content_); }

    void set_covered(bool covered) noexcept { covered_ = covered; }

    EventHandler* event_handler() noexcept override { return covered_ ? &blocker_ : nullptr; }
    void accessibility(AccessibilityInfo& out) const override { out.hidden = covered_; }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override { return content_->hit_test(point, path); }

private:
    std::unique_ptr<Element> content_;
    FocusableHandler blocker_{false};
    bool covered_ = false;
};

class NavigationStackElement final : public Element {
public:
    NavigationStackElement(std::unique_ptr<Element> root, LowerContext context)
        : root_{nullptr, std::make_unique<PageElement>(std::move(root)), nullptr}, context_(std::move(context)) {
        adopt(*root_.element);
    }

    // Shows the root followed by a page per item. Pages whose routes are unchanged from the start of
    // the path are kept; the rest are rebuilt.
    void set_pages(const std::vector<ForEachItem>& items);

    void inspect(Inspection& out) const override {
        out.name = "NavigationStack";
        out.detail = std::to_string(pages_.size() + 1) + (pages_.empty() ? " page" : " pages");
    }

    void for_each_child(const std::function<void(Element&)>& visit) override {
        visit(*root_.element);
        for (const Page& page : pages_) {
            visit(*page.element);
        }
        if (leaving_) {
            visit(*leaving_->element);
        }
    }
    bool drawing_moves() const override { return progress_.animating(); }

protected:
    Size measure(ProposedSize proposal) override {
        // It takes the space it's offered, so pages of different sizes don't resize it.
        const Size top = this->top().size_that_fits(proposal);
        const auto take = [](std::optional<float> offered, float fallback) {
            return offered && std::isfinite(*offered) ? *offered : fallback;
        };
        return {take(proposal.width, top.width), take(proposal.height, top.height)};
    }
    void arrange(Rect bounds, ProposedSize) override;
    void paint_impl(DisplayList& list) const override;
    bool hit_impl(Point point, std::vector<Element*>& path) override { return top().hit_test(point, path); }
    // While a transition runs, and once more after it to let go of a popped page.
    bool always_arranges() const override { return progress_.animating() || leaving_.has_value(); }

private:
    // Declaration order matters: the scope is destroyed before the element (see FragmentElement).
    struct Page {
        std::shared_ptr<const void> value; // the route, to tell when it changes
        std::unique_ptr<PageElement> element;
        std::unique_ptr<Scope> scope;
    };

    Page mount(const ForEachItem& item);
    PageElement& top() const { return pages_.empty() ? *root_.element : *pages_.back().element; }
    void tree_changed() const {
        if (context_.needs_frame) {
            *context_.needs_frame = true;
        }
        if (context_.tree_changed) {
            *context_.tree_changed = true;
        }
    }

    Page root_;
    std::vector<Page> pages_;
    LowerContext context_;
    // A transition: from `from_` (the page that was on top) to the top page. Popping, the top page
    // is revealed from under `from_`, which is `leaving_` until it has slid away.
    PageElement* from_ = nullptr;
    std::optional<Page> leaving_;
    bool popping_ = false;
    Animated<float> progress_{1.f};
    bool initialized_ = false;
};

bool same_route(const std::shared_ptr<const void>& old_value, const ForEachItem& item) {
    return old_value == item.value ||
           (old_value && item.value && item.equals && item.equals(old_value.get(), item.value.get()));
}

NavigationStackElement::Page NavigationStackElement::mount(const ForEachItem& item) {
    Page page{item.value, nullptr, std::make_unique<Scope>()};
    Scope::Use use(*page.scope);
    // Building may read state (component bodies); that mustn't subscribe the effect running this.
    std::unique_ptr<Element> content = untrack([&] { return lower(item.view(), context_); });
    page.element = std::make_unique<PageElement>(std::move(content));
    adopt(*page.element);
    return page;
}

void NavigationStackElement::set_pages(const std::vector<ForEachItem>& items) {
    // Restoring a path when the stack first mounts (a deep link) doesn't animate.
    static const Animation kDefault = Animation::smooth(0.35);
    const Animation* animation = nullptr;
    if (initialized_) {
        animation = change_animation();
        if (!animation) {
            animation = &kDefault;
        }
    }
    initialized_ = true;

    std::size_t kept = 0;
    while (kept < pages_.size() && kept < items.size() && same_route(pages_[kept].value, items[kept])) {
        ++kept;
    }
    if (kept == pages_.size() && kept == items.size()) {
        return;
    }

    PageElement* const old_top = &top();
    // The old top page may stay a little longer, to slide away; the other removed pages go now.
    std::optional<Page> removed_top;
    if (pages_.size() > kept) {
        removed_top = std::move(pages_.back());
        pages_.pop_back();
        pages_.erase(pages_.begin() + static_cast<std::ptrdiff_t>(kept), pages_.end());
    }
    for (std::size_t i = kept; i < items.size(); ++i) {
        pages_.push_back(mount(items[i]));
    }
    PageElement* const new_top = &top();

    root_.element->set_covered(root_.element.get() != new_top);
    for (const Page& page : pages_) {
        page.element->set_covered(page.element.get() != new_top);
    }
    old_top->set_covered(true);

    // Keyboard focus doesn't stay on a page that's out of sight. If it was showing (keyboard use),
    // it moves to the first control of the new page.
    if (FocusManager* focus = context_.focus; focus && focus->contains_focus(old_top)) {
        const bool visible = focus->ring_visible();
        focus->focus(visible ? FocusManager::first_focusable(*new_top) : nullptr, visible, visible);
    }

    // A pop reveals a page that was there; anything else (a push, a replaced route) slides a page in.
    popping_ = removed_top && kept == items.size();
    leaving_.reset(); // an unfinished transition ends here
    if (animation) {
        from_ = old_top;
        leaving_ = std::move(removed_top);
        progress_.set(0.f, nullptr);
        progress_.set(1.f, animation);
    } else {
        from_ = nullptr;
        progress_.set(1.f, nullptr);
    }
    invalidate_layout();
    tree_changed();
}

void NavigationStackElement::arrange(Rect bounds, ProposedSize) {
    if (!progress_.animating()) {
        from_ = nullptr;
        if (leaving_) {
            leaving_.reset();
            tree_changed();
        }
    }
    const ProposedSize proposal{bounds.width, bounds.height};
    top().place({bounds.x, bounds.y}, proposal);
    if (from_) {
        from_->place({bounds.x, bounds.y}, proposal);
    }
}

void NavigationStackElement::paint_impl(DisplayList& list) const {
    if (!from_ || !progress_.animating()) {
        top().paint(list);
        return;
    }
    list.animating = true;
    const Rect bounds = frame();
    // The page on top slides over the one underneath, which moves a little the same way and dims.
    const PageElement& under = popping_ ? top() : *from_;
    const PageElement& over = popping_ ? *from_ : top();
    const float in = popping_ ? 1 - progress_.current() : progress_.current(); // 1: `over` in place
    const auto paint_moved = [&](const PageElement& page, float dx) {
        DisplayList content;
        content.scale = list.scale;
        page.paint(content);
        list.animating |= content.animating;
        transform(content.commands, dx, 0, 1, Point{bounds.x, bounds.y});
        append_faded(list, std::move(content), 1);
    };

    list.commands.push_back(PushClipCommand{bounds, {}});
    paint_moved(under, -bounds.width * kUnderneathShift * in);
    list.commands.push_back(RectCommand{bounds, {}, ShapeStyle(Color::black.opacity(0.08f * in)), Color::clear, 0});
    // The page on top is opaque while it moves, and casts a soft shadow on the one underneath.
    const Rect over_rect{bounds.x + bounds.width * (1 - in), bounds.y, bounds.width, bounds.height};
    list.commands.push_back(BoxShadowCommand{over_rect, {}, Color::black.opacity(0.18f), 12});
    list.commands.push_back(
        RectCommand{over_rect, {}, ShapeStyle(context_.environment.color(ThemeColor::background)), Color::clear, 0});
    paint_moved(over, bounds.width * (1 - in));
    list.commands.push_back(PopClipCommand{});
}

class NavigationStackNode final : public ViewNode {
public:
    NavigationStackNode(View root, std::function<std::vector<ForEachItem>()> pages)
        : root_(std::move(root)), pages_(std::move(pages)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        const LowerContext nested = context.nested();
        auto element = std::make_unique<NavigationStackElement>(detail::lower(root_, nested), nested);
        NavigationStackElement* const target = element.get();
        // Reading the path subscribes this effect; building the pages doesn't (mount untracks).
        effect([target, pages = pages_] { target->set_pages(pages()); });
        return element;
    }

private:
    View root_;
    std::function<std::vector<ForEachItem>()> pages_;
};

// The bar over the pages: the top page's title, and once a page is pushed, a back button named
// after the page underneath.
View navigation_bar(std::function<std::vector<std::string>()> titles, std::function<void()> pop) {
    const auto current = [titles] { return titles().back(); };
    const auto previous = [titles] {
        const std::vector<std::string> all = titles();
        const std::string& title = all.size() > 1 ? all[all.size() - 2] : all.back();
        return title.empty() ? std::string("Back") : title;
    };
    const View back = Button(HStack({.spacing = 2}, Image("caret-left").font_weight(Weight::bold),
                                    Text(previous).line_limit(1)),
                             pop)
                          .button_style(PlainButtonStyle{})
                          .foreground_style(ThemeColor::accent)
                          .accessibility_label("Back")
                          .help("Back (Alt+Left)");
    return ZStack(Text(current).font(Font::headline).line_limit(1).padding(EdgeInsets::symmetric(96, 0)),
                  HStack(If([titles] { return titles().size() > 1; }, back), Spacer()))
        .padding(EdgeInsets::symmetric(12, 0))
        .frame({.max_width = infinity, .min_height = 44, .max_height = 44});
}

} // namespace

} // namespace elg::detail

namespace elg {

using detail::ViewAccess;

View detail::make_navigation_stack(View root, std::function<std::vector<ForEachItem>()> pages,
                                   std::function<std::vector<std::string>()> titles, std::function<void()> pop,
                                   bool shows_bar) {
    View stack = ViewAccess::make(std::make_shared<const NavigationStackNode>(std::move(root), std::move(pages)));
    if (shows_bar) {
        stack = VStack({.spacing = 0}, navigation_bar(titles, pop), Divider(), std::move(stack));
    }
    // Alt+Left goes back, as in browsers and file managers.
    return std::move(stack).on_key_press([titles, pop](const KeyEvent& event) {
        if (event.kind != KeyEvent::Kind::down || event.key != Key::left || !has(event.modifiers, KeyModifiers::alt) ||
            untrack([&] { return titles().size(); }) < 2) {
            return false;
        }
        pop();
        return true;
    });
}

} // namespace elg
