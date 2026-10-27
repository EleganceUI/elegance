// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// NavigationStack (design.md §13.5): pages driven by a path, coming and going by their transition,
// inside a NavigationStackStyle's body.

#include "view/view_node.h"
#include "view/window_drag.h"

#include <elg/animation.h>
#include <elg/navigation.h>
#include <elg/styles.h>
#include <elg/theme.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

namespace elg::detail {

namespace {

// How far the page underneath moves while another slides over it, as a fraction of the width.
constexpr float kUnderneathShift = 0.3f;
// Swiping back: let go past this fraction of the width, or faster than this (points per second)
// toward it, and the page goes; otherwise it slides back.
constexpr float kSwipeCommit = 0.35f;
constexpr float kSwipeFlick = 400;
constexpr double kSwipeVelocityWindow = 0.1; // the fingers' speed is their average over this long

double now_seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool contains(const Rect& rect, Point point) {
    return point.x >= rect.x && point.y >= rect.y && point.x < rect.max_x() && point.y < rect.max_y();
}

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

class NavigationStackElement final : public Element, private EventHandler {
public:
    NavigationStackElement(std::unique_ptr<Element> root, LowerContext context, std::function<void()> pop,
                           NavigationStackBehavior behavior)
        : root_{nullptr, std::make_unique<PageElement>(std::move(root)), nullptr}, context_(std::move(context)),
          pop_(std::move(pop)), behavior_(std::move(behavior)) {
        adopt(*root_.element);
    }

    EventHandler* event_handler() noexcept override { return this; }

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
    bool drawing_moves() const override { return progress_.animating() || swiping(); }

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
    bool hit_impl(Point point, std::vector<Element*>& path) override {
        // With a page to go back to, the whole stack takes two-finger swipes, not only its content.
        return top().hit_test(point, path) || (behavior_.back_gestures && !pages_.empty() && contains(frame(), point));
    }
    // While a transition runs, and once more after it to let go of a popped page.
    bool always_arranges() const override {
        return progress_.animating() || leaving_.has_value() || swiping();
    }

private:
    // Declaration order matters: the scope is destroyed before the element (see FragmentElement).
    struct Page {
        std::shared_ptr<const void> value; // the route, to tell when it changes
        std::unique_ptr<PageElement> element;
        std::unique_ptr<Scope> scope;
    };

    EventResult scroll(const ScrollEvent& event, Rect frame) override;

    Page mount(const ForEachItem& item);
    PageElement& top() const { return pages_.empty() ? *root_.element : *pages_.back().element; }
    // The page a swipe back reveals.
    PageElement& below() const {
        return pages_.size() < 2 ? *root_.element : *pages_[pages_.size() - 2].element;
    }
    // A swipe back is moving the top page, or it's sliding back after one.
    bool swiping() const { return swipe_active_ || swipe_.animating(); }
    // Paints `over` slid in by `in` (1: in place) over `under`.
    void paint_slide(DisplayList& list, const PageElement& under, const PageElement& over, float in) const;
    // Paints `over` faded in by `in` (1: opaque), `under` fading out.
    void paint_fade(DisplayList& list, const PageElement& under, const PageElement& over, float in) const;
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
    std::function<void()> pop_;
    NavigationStackBehavior behavior_;
    // Swiping back with two fingers: how far the top page has moved, as a fraction of the width.
    Animated<float> swipe_{0.f};
    bool swipe_active_ = false;   // following the fingers
    bool swipe_declined_ = false; // this gesture isn't a swipe back
    float swipe_start_ = 0;       // a pop that finishes a swipe starts here (progress_)
    struct Move {
        double time; // ScrollEvent::time
        float delta;
    };
    std::vector<Move> moves_;
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
    const Animation* animation = nullptr;
    if (initialized_ && behavior_.transition != PageTransition::none) {
        animation = change_animation();
        if (!animation && behavior_.animation) {
            animation = &*behavior_.animation;
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
    // A swipe back that let go far enough: the pop carries on from where the fingers left the page.
    const float start = std::exchange(swipe_start_, 0.f);
    swipe_active_ = false;
    swipe_.set(0.f, nullptr);
    if (animation || start > 0) {
        static const Animation kFinish = Animation::smooth(0.3);
        from_ = old_top;
        leaving_ = std::move(removed_top);
        progress_.set(popping_ ? start : 0.f, nullptr);
        progress_.set(1.f, start > 0 ? &kFinish : animation);
    } else {
        from_ = nullptr;
        progress_.set(1.f, nullptr);
    }
    invalidate_layout();
    tree_changed();
}

EventResult NavigationStackElement::scroll(const ScrollEvent& event, Rect) {
    if (!behavior_.back_gestures || event.phase == GesturePhase::none || pages_.empty() || progress_.animating()) {
        return {};
    }
    if (event.momentum) {
        return {.consumed = swipe_active_};
    }
    const float width = std::max(frame().width, 1.f);
    const double now = event.time > 0 ? event.time : now_seconds();
    if (event.phase == GesturePhase::began) {
        swipe_declined_ = false;
        moves_.clear();
        if (event.dx == 0 && event.dy == 0) {
            return {}; // fingers down: the scroll views under them hear it too
        }
    }
    if (swipe_declined_) {
        return {};
    }
    if (event.phase == GesturePhase::ended || event.phase == GesturePhase::cancelled) {
        if (!swipe_active_) {
            return {};
        }
        swipe_active_ = false;
        float velocity = 0; // toward going back
        if (moves_.size() > 1 && now - moves_.back().time < 0.06) {
            float distance = 0;
            for (std::size_t i = 1; i < moves_.size(); ++i) {
                distance += moves_[i].delta;
            }
            const double span = moves_.back().time - moves_.front().time;
            velocity = span > 0.008 ? static_cast<float>(-static_cast<double>(distance) / span) : 0.f;
        }
        const float moved = swipe_.target();
        if (event.phase == GesturePhase::ended && moved > 0 &&
            (moved > kSwipeCommit || velocity > kSwipeFlick)) {
            swipe_start_ = moved; // set_pages picks it up when the path changes
            pop_();
        } else {
            static const Animation kBack = Animation::smooth(0.3);
            swipe_.set(0.f, &kBack);
        }
        invalidate_layout();
        return {.consumed = true, .redraw = true};
    }
    if (!swipe_active_) {
        // A swipe back: sideways, fingers moving toward the start (dx < 0, like scrolling left).
        // Scroll views inside took it already if they could scroll that way.
        if (std::abs(event.dx) <= std::abs(event.dy) || event.dx >= 0) {
            swipe_declined_ = true;
            return {};
        }
        swipe_active_ = true;
        swipe_.set(0.f, nullptr);
    }
    std::erase_if(moves_, [&](const Move& move) { return move.time < now - kSwipeVelocityWindow; });
    moves_.push_back({now, event.dx});
    swipe_.set(std::clamp(swipe_.target() - event.dx / width, 0.f, 1.f), nullptr);
    invalidate_layout();
    return {.consumed = true, .redraw = true};
}

void NavigationStackElement::arrange(Rect bounds, ProposedSize) {
    if (swiping()) {
        const ProposedSize proposal{bounds.width, bounds.height};
        below().place({bounds.x, bounds.y}, proposal);
        top().place({bounds.x, bounds.y}, proposal);
        return;
    }
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
    if (swiping()) {
        list.animating |= swipe_.animating();
        paint_slide(list, below(), top(), 1 - swipe_.current());
        return;
    }
    if (!from_ || !progress_.animating()) {
        top().paint(list);
        return;
    }
    list.animating = true;
    // The page on top comes over the one underneath.
    const PageElement& under = popping_ ? top() : *from_;
    const PageElement& over = popping_ ? *from_ : top();
    const float in = popping_ ? 1 - progress_.current() : progress_.current();
    if (behavior_.transition == PageTransition::fade) {
        paint_fade(list, under, over, in);
    } else {
        paint_slide(list, under, over, in);
    }
}

void NavigationStackElement::paint_slide(DisplayList& list, const PageElement& under, const PageElement& over,
                                         float in) const {
    const Rect bounds = frame();
    // The one underneath moves a little the same way and dims.
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

void NavigationStackElement::paint_fade(DisplayList& list, const PageElement& under, const PageElement& over,
                                        float in) const {
    const auto paint_faded = [&](const PageElement& page, float opacity) {
        DisplayList content;
        content.scale = list.scale;
        page.paint(content);
        list.animating |= content.animating;
        append_faded(list, std::move(content), opacity);
    };
    list.commands.push_back(PushClipCommand{frame(), {}});
    paint_faded(under, 1 - in);
    paint_faded(over, in);
    list.commands.push_back(PopClipCommand{});
}

class NavigationStackNode final : public ViewNode {
public:
    NavigationStackNode(View root, std::function<std::vector<ForEachItem>()> pages, std::function<void()> pop,
                        NavigationStackBehavior behavior)
        : root_(std::move(root)), pages_(std::move(pages)), pop_(std::move(pop)), behavior_(std::move(behavior)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        const LowerContext nested = context.nested();
        auto element =
            std::make_unique<NavigationStackElement>(detail::lower(root_, nested), nested, pop_, behavior_);
        NavigationStackElement* const target = element.get();
        // Reading the path subscribes this effect; building the pages doesn't (mount untracks).
        effect([target, pages = pages_] { target->set_pages(pages()); });
        return element;
    }

private:
    View root_;
    std::function<std::vector<ForEachItem>()> pages_;
    std::function<void()> pop_;
    NavigationStackBehavior behavior_;
};

// The pages inside the style's body.
class StyledNavigationStackNode final : public ViewNode {
public:
    StyledNavigationStackNode(View pages, std::function<std::vector<std::string>()> titles, std::function<void()> pop)
        : pages_(std::move(pages)), titles_(std::move(titles)), pop_(std::move(pop)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        const EnvironmentValues env = context.environment;
        const std::shared_ptr<const NavigationStackStyle> style =
            untrack([&] { return env.get<NavigationStackStyleKey>(); });
        View body;
        {
            const EnvironmentScope scope(env);
            body = style->make_body({
                .pages = pages_,
                .titles = titles_,
                .can_go_back = [titles = titles_] { return titles().size() > 1; },
                .back = pop_,
            });
        }
        return detail::lower(body, context);
    }

private:
    View pages_;
    std::function<std::vector<std::string>()> titles_;
    std::function<void()> pop_;
};

} // namespace

} // namespace elg::detail

namespace elg {

using detail::ViewAccess;

NavigationStackStyle::~NavigationStackStyle() = default;

std::shared_ptr<const NavigationStackStyle> NavigationStackStyleKey::default_value(const EnvironmentValues&) {
    static const auto style = std::make_shared<const PlainNavigationStackStyle>();
    return style;
}

View PlainNavigationStackStyle::make_body(const NavigationStackConfiguration& stack) const {
    return stack.pages;
}

View BarNavigationStackStyle::make_body(const NavigationStackConfiguration& stack) const {
    const BarNavigationStackStyleOptions& o = options_;
    const Signal<std::vector<std::string>> titles = stack.titles;
    const auto current = [titles] { return titles().back(); };
    const auto previous = [titles] {
        const std::vector<std::string> all = titles();
        const std::string& title = all.size() > 1 ? all[all.size() - 2] : all.back();
        return title.empty() ? std::string("Back") : title;
    };
    const View back = Button(HStack({.spacing = 2}, Image("caret-left").font_weight(Weight::bold),
                                    Text(previous).line_limit(1)),
                             stack.back)
                          .button_style(PlainButtonStyle{})
                          .foreground_style(o.back_color.value_or(Prop<Color>([](const EnvironmentValues& env) {
                              return env.get<TintKey>();
                          })))
                          .accessibility_label("Back")
                          .help("Back (Alt+Left)");
    const View bar = ZStack(Text(current).font(o.title_font).line_limit(1).padding(EdgeInsets::symmetric(96, 0)),
                            HStack(If(stack.can_go_back, back), Spacer()))
                         .padding(EdgeInsets::symmetric(12, 0))
                         .frame({.max_width = infinity, .min_height = o.height, .max_height = o.height})
                         .background(Prop<ShapeStyle>([background = o.background](const EnvironmentValues& env) {
                             return ShapeStyle(background.get(env));
                         }));
    if (!o.shows_divider) {
        return VStack({.spacing = 0}, bar, stack.pages);
    }
    return VStack({.spacing = 0}, bar, Separator(), stack.pages);
}

View detail::make_navigation_stack(View root, std::function<std::vector<ForEachItem>()> pages,
                                   std::function<std::vector<std::string>()> titles, std::function<void()> pop,
                                   NavigationStackBehavior behavior) {
    const bool back_gestures = behavior.back_gestures;
    const View pages_view = ViewAccess::make(
        std::make_shared<const NavigationStackNode>(std::move(root), std::move(pages), pop, std::move(behavior)));
    View stack = ViewAccess::make(std::make_shared<const StyledNavigationStackNode>(pages_view, titles, pop));
    if (!back_gestures) {
        return stack;
    }
    // Alt+Left goes back, as in browsers and file managers.
    return std::move(stack).on_key([titles, pop](const KeyEvent& event) {
        if (event.kind != KeyEvent::Kind::down || event.key != Key::left || !has(event.modifiers, KeyModifiers::alt) ||
            untrack([&] { return titles().size(); }) < 2) {
            return false;
        }
        pop();
        return true;
    });
}

} // namespace elg
