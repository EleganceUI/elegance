// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Floating content (View::floating()) and the popover built on it (design.md §13.1).

#include "view/floating.h"

#include "view/view_node.h"
#include "view/window_services.h"

#include <elg/error.h>
#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/text.h>
#include <elg/views.h>

#include <algorithm>
#include <atomic>

namespace elg::detail {

// --- Placement ---

namespace {

FloatingEdge opposite(FloatingEdge edge) {
    switch (edge) {
    case FloatingEdge::bottom: return FloatingEdge::top;
    case FloatingEdge::top: return FloatingEdge::bottom;
    case FloatingEdge::trailing: return FloatingEdge::leading;
    case FloatingEdge::leading: return FloatingEdge::trailing;
    }
    return edge;
}

} // namespace

Point place_floating(Size size, Rect anchor, const FloatingPlacement& placement, Rect area) {
    Point origin;
    if (placement.position) {
        origin = {anchor.x + placement.position->x, anchor.y + placement.position->y};
    } else {
        const float spacing = placement.spacing;
        // Along the edge's outward direction: where it starts, and whether it fits there.
        const auto along = [&](FloatingEdge edge) {
            switch (edge) {
            case FloatingEdge::bottom: return anchor.max_y() + spacing;
            case FloatingEdge::top: return anchor.y - spacing - size.height;
            case FloatingEdge::trailing: return anchor.max_x() + spacing;
            case FloatingEdge::leading: return anchor.x - spacing - size.width;
            }
            return 0.f;
        };
        const auto fits = [&](FloatingEdge edge, float at) {
            switch (edge) {
            case FloatingEdge::bottom: return at + size.height <= area.max_y();
            case FloatingEdge::top: return at >= area.y;
            case FloatingEdge::trailing: return at + size.width <= area.max_x();
            case FloatingEdge::leading: return at >= area.x;
            }
            return true;
        };
        FloatingEdge edge = placement.edge;
        float normal = along(edge);
        if (!fits(edge, normal) && fits(opposite(edge), along(opposite(edge)))) {
            edge = opposite(edge);
            normal = along(edge);
        }

        // Across: lined up with the anchor, moved to its other end when that fits and this doesn't.
        const bool vertical = edge == FloatingEdge::bottom || edge == FloatingEdge::top;
        const float start = vertical ? anchor.x : anchor.y;
        const float extent = vertical ? anchor.width : anchor.height;
        const float length = vertical ? size.width : size.height;
        const float area_start = vertical ? area.x : area.y;
        const float area_end = vertical ? area.max_x() : area.max_y();
        float across = start;
        switch (placement.alignment) {
        case FloatingAlignment::start:
            across = start;
            if (across + length > area_end && start + extent - length >= area_start) {
                across = start + extent - length;
            }
            break;
        case FloatingAlignment::center: across = start + (extent - length) / 2; break;
        case FloatingAlignment::end:
            across = start + extent - length;
            if (across < area_start && start + length <= area_end) {
                across = start;
            }
            break;
        }
        origin = vertical ? Point{across, normal} : Point{normal, across};
    }
    origin.x += placement.offset.x;
    origin.y += placement.offset.y;
    origin.x = std::clamp(origin.x, area.x, std::max(area.x, area.max_x() - size.width));
    origin.y = std::clamp(origin.y, area.y, std::max(area.y, area.max_y() - size.height));
    return origin;
}

std::uint64_t next_floating_token() noexcept {
    static std::atomic<std::uint64_t> next{1};
    return next++;
}

// --- The layer ---

FloatingLayerElement::FloatingLayerElement(std::unique_ptr<Element> content) : content_(std::move(content)) {
    adopt(*content_);
}

FloatingLayerElement::~FloatingLayerElement() {
    // Panels first (their content may hold floating content of its own, which removes itself
    // here), then the window's content, whose floating views find nothing left to remove.
    while (!panels_.empty()) {
        const Panel panel = std::move(panels_.back());
        panels_.pop_back();
    }
    closing_ = true;
    content_.reset();
}

FloatingLayerElement* FloatingLayerElement::of(Element& element) {
    for (Element* e = &element; e; e = e->parent()) {
        if (auto* layer = dynamic_cast<FloatingLayerElement*>(e)) {
            return layer;
        }
    }
    return nullptr;
}

void FloatingLayerElement::add(Panel panel) {
    if (panel.element) {
        adopt(*panel.element);
    }
    panels_.push_back(std::move(panel));
    invalidate_layout();
}

void FloatingLayerElement::remove(std::uint64_t token) {
    if (closing_) {
        return;
    }
    const auto it = std::ranges::find(panels_, token, &Panel::token);
    if (it == panels_.end()) {
        return;
    }
    // Out of the list before it's destroyed: floating content inside it removes itself too.
    const Panel panel = std::move(*it);
    panels_.erase(it);
    invalidate_layout();
}

void FloatingLayerElement::for_each_child(const std::function<void(Element&)>& visit) {
    visit(*content_);
    for (const Panel& panel : panels_) {
        if (panel.element) {
            visit(*panel.element);
        }
    }
}

void FloatingLayerElement::arrange(Rect bounds, ProposedSize proposal) {
    content_->place({bounds.x, bounds.y}, proposal);
    // Then the panels, by their anchors as laid out now: at their own size, up to the window's.
    for (const Panel& panel : panels_) {
        if (!panel.element) {
            continue;
        }
        Size size = panel.element->size_that_fits({});
        if (size.width > bounds.width || size.height > bounds.height) {
            size = panel.element->size_that_fits({std::min(size.width, bounds.width), std::min(size.height, bounds.height)});
        }
        const Point origin = place_floating(size, panel.anchor->drawn_frame(), panel.placement, bounds);
        panel.element->place(origin, ProposedSize{size.width, size.height});
    }
}

void FloatingLayerElement::paint_impl(DisplayList& list) const {
    content_->paint(list);
    for (const Panel& panel : panels_) {
        if (panel.element) {
            panel.element->paint(list);
        }
    }
}

bool FloatingLayerElement::hit_impl(Point point, std::vector<Element*>& path) {
    for (auto it = panels_.rbegin(); it != panels_.rend(); ++it) {
        if (it->element && it->interactive && it->element->hit_test(point, path)) {
            return true;
        }
    }
    // Outside the panels while one closes on outside clicks: the press is the layer's.
    if (top(false)) {
        return true;
    }
    return content_->hit_test(point, path);
}

FloatingLayerElement::Panel* FloatingLayerElement::top(bool escape) {
    for (auto it = panels_.rbegin(); it != panels_.rend(); ++it) {
        if (escape ? it->dismiss.escape : it->dismiss.outside_click && it->element) {
            return &*it;
        }
    }
    return nullptr;
}

void FloatingLayerElement::dismiss(Panel& panel) {
    // Copied first: the callback (the binding going false) may change the list.
    const std::uint64_t token = panel.token;
    const std::function<void()> dismissed = panel.dismissed;
    if (dismissed) {
        dismissed();
    }
    remove(token);
}

EventResult FloatingLayerElement::pointer(const PointerEvent& event, Rect) {
    if (event.kind != PointerEvent::Kind::down) {
        return {};
    }
    if (Panel* panel = top(false)) {
        dismiss(*panel);
        return {.consumed = true, .redraw = true};
    }
    return {};
}

EventResult FloatingLayerElement::key(const KeyEvent& event) {
    if (event.kind != KeyEvent::Kind::down || event.key != Key::escape) {
        return {};
    }
    if (Panel* panel = top(true)) {
        dismiss(*panel);
        return {.consumed = true, .redraw = true};
    }
    return {};
}

// --- Floating content ---

namespace {

// The view floating content belongs to: opens it while the binding is true, and closes it when
// the binding goes false or the view goes away.
class FloatingElement final : public Element {
public:
    // Shared with the content's close callbacks, which may run after this element is gone.
    struct Presence {
        bool open = false;   // opened and not closed since
        bool wanted = false; // the binding's value, as last seen
        int generation = 0;  // +1 per open and close(), so a stale callback does nothing
    };

    FloatingElement(std::unique_ptr<Element> content, std::function<View()> floating, FloatingOptions options,
                    Binding<bool> is_presented, const LowerContext& context)
        : content_(std::move(content)), floating_(std::move(floating)), options_(std::move(options)),
          is_presented_(std::move(is_presented)), context_(context.nested()),
          level_(context.services ? context.services->menu_level() + 1 : 0) {
        adopt(*content_);
    }
    ~FloatingElement() override {
        close();
        if (layer_) {
            layer_->remove(token_); // an Esc entry for a popup the app closed
        }
    }

    // The binding's value (from an effect). Opening waits for a frame to anchor to.
    void set_presented(bool presented) {
        presence_->wanted = presented;
        if (!presented) {
            close();
        } else if (arranged_) {
            open();
        }
    }

    float layout_priority() const noexcept override { return content_->layout_priority(); }
    bool is_spacer() const noexcept override { return content_->is_spacer(); }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }
    void inspect(Inspection& out) const override { out.name = "Floating"; }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override {
        content_->place({bounds.x, bounds.y}, proposal);
        arranged_ = true;
        if (presence_->wanted && !presence_->open) {
            open(); // presented from the start
        } else if (presence_->open && hosted_on_screen() && context_.services && drawn_frame() != anchor_) {
            // The view moved (scrolled, resized): its popup follows.
            anchor_ = drawn_frame();
            context_.services->move_popup(token_, anchor_);
        }
    }
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override { return content_->hit_test(point, path); }

private:
    bool hosted_on_screen() const { return options_.boundary == FloatingBoundary::screen; }
    // Modal: the app's popup stack (menus, popovers), which takes the pointer while open.
    bool modal() const { return hosted_on_screen() && options_.dismiss.outside_click; }

    // Closed by the user or the system: the binding goes back to false, which then finds nothing
    // left to close.
    std::function<void()> on_dismissed() {
        return [presence = presence_, binding = is_presented_, generation = presence_->generation] {
            if (presence->generation != generation) {
                return;
            }
            presence->open = false;
            ++presence->generation;
            if (presence->wanted) {
                presence->wanted = false;
                binding.set(false);
            }
        };
    }

    void open() {
        if (presence_->open) {
            return;
        }
        presence_->open = true;
        ++presence_->generation;
        token_ = next_floating_token();
        anchor_ = drawn_frame();
        layer_ = FloatingLayerElement::of(*this);
        if (hosted_on_screen()) {
            if (!context_.services) {
                return;
            }
            context_.services->open_popup({
                .kind = PopupRequest::Kind::floating,
                .placement = options_.placement,
                .anchor = anchor_,
                .content = floating_,
                .environment = context_.environment,
                .level = level_,
                .on_close = on_dismissed(),
                .modal = modal(),
                .token = token_,
            });
            // Esc closes a popup that leaves the window usable through the window's layer.
            if (!modal() && layer_ && options_.dismiss.escape) {
                layer_->add({.token = token_,
                             .anchor = this,
                             .dismiss = {.outside_click = false, .escape = true},
                             .dismissed = [dismissed = on_dismissed(), services = context_.services, token = token_] {
                                 dismissed();
                                 services->close_popup(token);
                             }});
            }
            return;
        }
        if (!layer_) {
            return;
        }
        FloatingLayerElement::Panel panel{.token = token_,
                                          .anchor = this,
                                          .placement = options_.placement,
                                          .dismiss = options_.dismiss,
                                          .dismissed = on_dismissed(),
                                          .scope = std::make_unique<Scope>()};
        {
            Scope::Use use(*panel.scope);
            const EnvironmentScope environment(context_.environment);
            panel.element = detail::lower(floating_(), context_);
        }
        layer_->add(std::move(panel));
        changed();
    }

    void close() {
        if (!std::exchange(presence_->open, false)) {
            return;
        }
        ++presence_->generation;
        if (layer_) {
            layer_->remove(token_);
        }
        if (hosted_on_screen() && context_.services) {
            if (modal()) {
                context_.services->close_menus(level_);
            } else {
                context_.services->close_popup(token_);
            }
        }
        changed();
    }

    // Elements came or went: a new frame, and the event dispatcher forgets removed ones.
    void changed() const {
        if (context_.tree_changed) {
            *context_.tree_changed = true;
        }
        if (context_.needs_frame) {
            *context_.needs_frame = true;
        }
    }

    std::unique_ptr<Element> content_;
    std::function<View()> floating_;
    FloatingOptions options_;
    Binding<bool> is_presented_;
    LowerContext context_;
    int level_;
    std::shared_ptr<Presence> presence_ = std::make_shared<Presence>();
    std::uint64_t token_ = 0;
    Rect anchor_;
    FloatingLayerElement* layer_ = nullptr;
    bool arranged_ = false;
};

class FloatingNode final : public ViewNode {
public:
    FloatingNode(View content, Binding<bool> is_presented, std::function<View()> floating, FloatingOptions options)
        : content_(std::move(content)), is_presented_(std::move(is_presented)), floating_(std::move(floating)),
          options_(std::move(options)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        auto element = std::make_unique<FloatingElement>(detail::lower(content_, context), floating_, options_,
                                                         is_presented_, context);
        FloatingElement* const target = element.get();
        detail::bind(Prop<bool>(is_presented_), context, [target](bool presented) { target->set_presented(presented); });
        return element;
    }

private:
    View content_;
    Binding<bool> is_presented_;
    std::function<View()> floating_;
    FloatingOptions options_;
};

// A tooltip's view: shows it once the pointer has rested there for the delay, hides it when the
// pointer leaves or presses.
class TooltipElement final : public Element, private EventHandler {
public:
    TooltipElement(std::unique_ptr<Element> content, std::function<View()> tooltip, TooltipOptions options,
                   const LowerContext& context)
        : content_(std::move(content)), tooltip_(std::move(tooltip)), options_(std::move(options)),
          context_(context.nested()) {
        adopt(*content_);
    }
    ~TooltipElement() override { hide(); }

    EventHandler* event_handler() noexcept override { return this; }
    HoverDelay effective_hover_delay() const override { return {.enter = options_.delay, .exit = 0}; }
    float layout_priority() const noexcept override { return content_->layout_priority(); }
    bool is_spacer() const noexcept override { return content_->is_spacer(); }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }
    void inspect(Inspection& out) const override { out.name = "Tooltip"; }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override { return content_->hit_test(point, path); }

private:
    EventResult hover(bool inside) override {
        if (inside) {
            show();
        } else {
            hide();
        }
        return {.redraw = true};
    }
    EventResult pointer(const PointerEvent& event, Rect) override {
        if (event.kind == PointerEvent::Kind::down) {
            hide();
        }
        return {};
    }

    // The content in the environment's TooltipStyle, where the tooltip opens.
    std::function<View()> styled() const {
        return [content = tooltip_] {
            const EnvironmentValues environment = styling_environment();
            const auto style = untrack([&] { return environment.get<TooltipStyleKey>(); });
            return style->make_body({.content = content()});
        };
    }

    void show() {
        if (shown_) {
            return;
        }
        shown_ = true;
        if (options_.boundary == FloatingBoundary::screen) {
            if (context_.services) {
                context_.services->open_popup({.kind = PopupRequest::Kind::tooltip,
                                               .placement = options_.placement,
                                               .anchor = drawn_frame(),
                                               .content = styled(),
                                               .environment = context_.environment});
            }
            return;
        }
        layer_ = FloatingLayerElement::of(*this);
        if (!layer_) {
            return;
        }
        token_ = next_floating_token();
        FloatingLayerElement::Panel panel{.token = token_,
                                          .anchor = this,
                                          .placement = options_.placement,
                                          .dismiss = {.outside_click = false, .escape = false},
                                          .interactive = false,
                                          .scope = std::make_unique<Scope>()};
        {
            Scope::Use use(*panel.scope);
            const EnvironmentScope environment(context_.environment);
            panel.element = detail::lower(styled()(), context_);
        }
        layer_->add(std::move(panel));
        changed();
    }

    void hide() {
        if (!std::exchange(shown_, false)) {
            return;
        }
        if (layer_) {
            layer_->remove(token_);
            changed();
        } else if (context_.services) {
            context_.services->close_tooltip();
        }
    }

    void changed() const {
        if (context_.tree_changed) {
            *context_.tree_changed = true;
        }
        if (context_.needs_frame) {
            *context_.needs_frame = true;
        }
    }

    std::unique_ptr<Element> content_;
    std::function<View()> tooltip_;
    TooltipOptions options_;
    LowerContext context_;
    FloatingLayerElement* layer_ = nullptr;
    std::uint64_t token_ = 0;
    bool shown_ = false;
};

class TooltipNode final : public ViewNode {
public:
    TooltipNode(View content, std::function<View()> tooltip, TooltipOptions options)
        : content_(std::move(content)), tooltip_(std::move(tooltip)), options_(std::move(options)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        return std::make_unique<TooltipElement>(detail::lower(content_, context), tooltip_, options_, context);
    }

private:
    View content_;
    std::function<View()> tooltip_;
    TooltipOptions options_;
};

} // namespace

EnvironmentValues styling_environment() {
    const EnvironmentValues* environment = current_environment();
    return environment ? *environment : EnvironmentValues();
}

} // namespace elg::detail

namespace elg {

View View::floating(Binding<bool> is_presented, std::function<View()> content, FloatingOptions options) const {
    if (!content) {
        throw UsageError("floating() needs a content function");
    }
    return detail::ViewAccess::make(std::make_shared<const detail::FloatingNode>(*this, std::move(is_presented),
                                                                                std::move(content), std::move(options)));
}

View View::popover(Binding<bool> is_presented, std::function<View()> content, PopoverOptions options) const {
    // The content in the environment's PopoverStyle, where the popover opens.
    auto panel = [content = std::move(content), padded = options.padded] {
        const EnvironmentValues environment = detail::styling_environment();
        const auto style = untrack([&] { return environment.get<PopoverStyleKey>(); });
        return style->make_body({.content = content(), .is_padded = [padded, environment] { return padded.get(environment); }});
    };
    return floating(std::move(is_presented), std::move(panel),
                    {.placement = options.placement, .boundary = options.boundary});
}

View View::tooltip(std::function<View()> content, TooltipOptions options) const {
    if (!content) {
        throw UsageError("tooltip() needs a content function");
    }
    return detail::ViewAccess::make(std::make_shared<const detail::TooltipNode>(*this, std::move(content), std::move(options)));
}

View View::help(Prop<std::string> text) const {
    return tooltip([text = std::move(text)] { return Text(text); });
}

// --- Styles ---

View DefaultPopoverStyle::make_body(const PopoverConfiguration& popover) const {
    const Signal<bool> padded = popover.is_padded;
    return popover.content.padding(Prop<float>([padded] { return padded() ? 12.f : 0.f; }))
        .clip_shape(RoundedRectangle(10))
        .background(ThemeColor::surface, RoundedRectangle(10))
        .overlay(RoundedRectangle(10).stroke_border(ThemeColor::outline, 1))
        .shadow({.color = Color::black.opacity(0.28f), .radius = 12, .y = 3});
}

View PlainPopoverStyle::make_body(const PopoverConfiguration& popover) const {
    return popover.content;
}

View DefaultTooltipStyle::make_body(const TooltipConfiguration& tooltip) const {
    return tooltip.content.font(Font::system(12))
        .foreground_style(ThemeColor::primary)
        .padding(EdgeInsets::symmetric(8, 5))
        .background(ThemeColor::surface, RoundedRectangle(5))
        .overlay(RoundedRectangle(5).stroke_border(ThemeColor::outline, 1))
        .shadow({.color = Color::black.opacity(0.28f), .radius = 6, .y = 3});
}

View PlainTooltipStyle::make_body(const TooltipConfiguration& tooltip) const {
    return tooltip.content;
}

} // namespace elg
