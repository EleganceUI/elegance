// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Menus, context menus, tooltips, and the menu Picker (design.md §13.1). The popups themselves
// are windows the app opens on request (WindowServices::open_popup); these are the views in and
// around them.

#include "view/icons.h"
#include "view/accessibility.h"
#include "view/view_node.h"

#include <elg/animation.h>
#include <elg/error.h>
#include <elg/font.h>
#include <elg/image.h>
#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/text.h>
#include <elg/views.h>

#include <filesystem>

#if defined(_WIN32)
#include <windows.h> // the build defines WIN32_LEAN_AND_MEAN and NOMINMAX
#endif

namespace elg::detail {

// --- Icons ---

namespace {

constexpr std::string_view kIconFamily = "elg.caption-glyphs";

bool icon_font_available() {
    static const bool available = [] {
#if defined(_WIN32)
        wchar_t windows[MAX_PATH] = L"C:\\Windows";
        GetWindowsDirectoryW(windows, MAX_PATH);
        const std::filesystem::path file = std::filesystem::path(windows) / "Fonts" / "segmdl2.ttf";
        std::error_code error;
        if (std::filesystem::exists(file, error)) {
            register_font(std::string(kIconFamily), file);
            return true;
        }
#endif
        return false;
    }();
    return available;
}

} // namespace

namespace {

// The built-in set's icon, or `fallback` when the library was built without icons.
View set_icon(std::string_view name, float size, Weight weight, View fallback) {
    try {
        return Image(name).font(Font::system(size, weight));
    } catch (const UsageError&) {
        return fallback;
    }
}

} // namespace

View icon(Icon which, float size, Weight weight) {
    const bool caption = which == Icon::minimize || which == Icon::maximize || which == Icon::restore ||
                         which == Icon::close;
    if (caption && icon_font_available()) {
        const char* glyph = "";
        switch (which) {
        case Icon::minimize: glyph = "\xEE\xA4\xA1"; break; // U+E921 ChromeMinimize
        case Icon::maximize: glyph = "\xEE\xA4\xA2"; break; // U+E922 ChromeMaximize
        case Icon::restore: glyph = "\xEE\xA4\xA3"; break;  // U+E923 ChromeRestore
        default: glyph = "\xEE\xA2\xBB"; break;             // U+E8BB ChromeClose
        }
        return Text(glyph).font(Font::custom(std::string(kIconFamily), size));
    }
    switch (which) {
    case Icon::minimize: return set_icon("phosphor.minus", size * 1.4f, weight, Rectangle().frame(size, 1));
    case Icon::maximize:
        return set_icon("phosphor.square", size * 1.4f, weight,
                        Rectangle().stroke_border(ThemeColor::primary, 1).frame(size, size));
    case Icon::restore:
        return set_icon("phosphor.copy-simple", size * 1.4f, weight,
                        Rectangle().stroke_border(ThemeColor::primary, 1).frame(size, size));
    case Icon::close:
        return set_icon("phosphor.x", size * 1.4f, weight,
                        Text("\xC3\x97").font(Font::system(size * 1.6f, Weight::light))); // ×
    case Icon::check: return set_icon("phosphor.check", size, weight, Circle().frame(size * 0.55f, size * 0.55f));
    case Icon::chevron_right:
        return set_icon("phosphor.caret-right", size, weight, Text("\xE2\x80\xBA").font(Font::system(size * 1.6f)));
    case Icon::chevron_down:
        return set_icon("phosphor.caret-down", size, weight, Text("v").font(Font::system(size * 1.2f)));
    case Icon::chevron_up:
        return set_icon("phosphor.caret-up", size, weight, Text("^").font(Font::system(size * 1.2f)));
    }
    return EmptyView();
}

namespace {

// --- Menu items ---

constexpr float kItemRadius = 5;

// A menu row's look: highlighted while hovered or focused (keyboard), dimmed when disabled.
struct RowState {
    State<bool> hovered{false};
    State<bool> focused{false};
};

View menu_row(View content, RowState state, Prop<bool> destructive, EnvironmentValues environment) {
    const auto enabled = [environment] { return !environment.get<DisabledKey>(); };
    return content.padding(EdgeInsets::symmetric(10, 6))
        .frame({.max_width = infinity, .alignment = Alignment::leading})
        .foreground_style([=](const EnvironmentValues& env) {
            if (!enabled()) {
                return env.color(ThemeColor::tertiary);
            }
            return destructive.get(env) ? env.color(ThemeColor::danger) : env.color(ThemeColor::primary);
        })
        .background(
            [=](const EnvironmentValues& env) {
                const bool lit = enabled() && (state.hovered() || state.focused());
                return lit ? env.color(ThemeColor::outline).opacity(0.55f) : Color::clear;
            },
            RoundedRectangle(kItemRadius));
}

// The row as a button: hovering focuses it (so keys continue from there) and closes submenus
// deeper than this menu; `action` runs on click, Enter, or Space.
// `closes_deeper`: hovering closes submenus deeper than this menu (not for a submenu's own row,
// which would otherwise close the submenu it opened whenever the pointer comes back to it).
std::unique_ptr<ButtonElement> make_row_element(const View& row, RowState state, std::function<void()> action,
                                                bool closes_deeper, const LowerContext& context) {
    const State<bool> pressed(false);
    auto element = make_button_element(row, std::move(action), pressed, state.hovered, context);
    element->set_focus_state(state.focused);
    if (context.focus) {
        element->set_focus_manager(context.focus, {}); // the highlight shows focus, not a ring
    }
    ButtonElement* const target = element.get();
    FocusManager* const focus = context.focus;
    WindowServices* const services = context.services;
    element->set_on_hover([target, focus, services, closes_deeper](bool inside) {
        if (!inside) {
            return;
        }
        if (focus) {
            focus->focus(target, false);
        }
        if (closes_deeper && services && services->menu_level() >= 0) {
            services->close_menus(services->menu_level() + 1);
        }
    });
    return element;
}

class MenuItemNode final : public ViewNode {
public:
    MenuItemNode(Prop<std::string> title, std::function<void()> action, MenuItemOptions options)
        : title_(std::move(title)), action_(std::move(action)), options_(std::move(options)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        const RowState state;
        std::vector<View> parts;
        if (options_.checked) {
            const Prop<bool> checked = *options_.checked;
            parts.push_back(If(checked, icon(Icon::check, 13, Weight::bold), EmptyView()).frame(14, 14));
        }
        if (options_.image) {
            parts.push_back(Image(*options_.image).font(Font::system(15)).frame(16, 16));
        }
        parts.push_back(Text(title_));
        parts.push_back(Spacer(24));
        const Prop<std::string>& shortcut = options_.shortcut;
        const View hint = Text(shortcut).font(Font::system(12)).foreground_style(ThemeColor::tertiary);
        if (const std::string* value = shortcut.static_value()) {
            if (!value->empty()) {
                parts.push_back(hint);
            }
        } else {
            parts.push_back(If([shortcut](const EnvironmentValues& env) { return !shortcut.get(env).empty(); }, hint));
        }
        const View row = menu_row(HStackLayout(VerticalAlignment::center, 8)(std::move(parts)), state,
                                  options_.destructive, context.environment);
        // Chosen: the menus close first, then the action runs (it may open a dialog or a menu).
        WindowServices* const services = context.services;
        auto action = [services, action = action_] {
            if (services) {
                services->close_menus_then(action);
            } else if (action) {
                action();
            }
        };
        return make_row_element(row, state, std::move(action), true, context);
    }

private:
    Prop<std::string> title_;
    std::function<void()> action_;
    MenuItemOptions options_;
};

// Calls `action` when the pointer has rested on the view for a moment (submenu opening).
class DelayedHover final : public EventHandler {
public:
    explicit DelayedHover(std::function<void(bool)> action) : action_(std::move(action)) {}
    std::optional<HoverDelay> hover_delay() const override { return HoverDelay{.enter = 0.25, .exit = 0}; }
    EventResult hover(bool inside) override {
        action_(inside);
        return {};
    }

private:
    std::function<void(bool)> action_;
};

class SubmenuNode final : public ViewNode {
public:
    SubmenuNode(Prop<std::string> title, std::function<View()> menu) : title_(std::move(title)), menu_(std::move(menu)) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        const RowState state;
        const View row = menu_row(HStack({.spacing = 8}, Text(title_), Spacer(24), icon(Icon::chevron_right, 12)), state,
                                  false, context.environment);
        WindowServices* const services = context.services;
        const int level = services ? services->menu_level() + 1 : 0;
        auto element_slot = std::make_shared<Element*>(nullptr);
        auto open = [services, level, element_slot, menu = menu_, environment = context.environment] {
            if (services && *element_slot) {
                services->open_popup({.kind = PopupRequest::Kind::menu,
                                      .placement = PopupRequest::Placement::beside,
                                      .anchor = (*element_slot)->drawn_frame(),
                                      .content = menu,
                                      .environment = environment,
                                      .level = level});
            }
        };
        // Click, Enter, Space, and Right open it at once; resting the pointer on it opens it after
        // a moment (so sweeping across rows doesn't flash submenus open).
        auto element = make_row_element(row, state, open, false, context);
        element->set_extra_key(Key::right);
        *element_slot = element.get();
        auto opener = std::make_unique<DelayedHover>([open](bool inside) {
            if (inside) {
                open();
            }
        });
        const EnvironmentValues environment = context.environment;
        return std::make_unique<EventElement>(std::move(opener), std::move(element),
                                              [environment] { return environment.get<HoverDelayKey>(); });
    }

private:
    Prop<std::string> title_;
    std::function<View()> menu_;
};

// --- Openers ---

// Wraps a view; the handler sees its frame. Pointer events pass on unless handled.
class PopupTriggerElement final : public Element, private EventHandler {
public:
    enum class Kind { context_menu, help };

    PopupTriggerElement(Kind kind, std::unique_ptr<Element> content, std::function<View()> popup,
                        EnvironmentValues environment, WindowServices* services)
        : kind_(kind), content_(std::move(content)), popup_(std::move(popup)), environment_(std::move(environment)),
          services_(services) {
        adopt(*content_);
    }

    EventHandler* event_handler() noexcept override { return this; }
    HoverDelay effective_hover_delay() const override {
        return kind_ == Kind::help ? HoverDelay{.enter = 0.6, .exit = 0} : environment_.get<HoverDelayKey>();
    }
    float layout_priority() const noexcept override { return content_->layout_priority(); }
    bool is_spacer() const noexcept override { return content_->is_spacer(); }
    void for_each_child(const std::function<void(Element&)>& visit) override { visit(*content_); }

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override { content_->place({bounds.x, bounds.y}, proposal); }
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override { return content_->hit_test(point, path); }

private:
    void open(PopupRequest::Kind kind, PopupRequest::Placement placement, Rect anchor) {
        if (services_) {
            services_->open_popup({.kind = kind,
                                   .placement = placement,
                                   .anchor = anchor,
                                   .content = popup_,
                                   .environment = environment_,
                                   .level = services_->menu_level() + 1});
        }
    }

    EventResult pointer(const PointerEvent& event, Rect) override {
        if (kind_ == Kind::help) {
            if (event.kind == PointerEvent::Kind::down && services_) {
                services_->close_tooltip();
            }
            return {};
        }
        if (event.button != MouseButton::right) {
            return {};
        }
        // Opens on release, like Windows; the press is taken so nothing else reacts to it.
        if (event.kind == PointerEvent::Kind::up) {
            // The event is in layout coordinates; the menu opens where the pointer is on screen.
            const Rect drawn = drawn_frame();
            const Rect laid_out = frame();
            open(PopupRequest::Kind::menu, PopupRequest::Placement::at_point,
                 {event.location.x + drawn.x - laid_out.x, event.location.y + drawn.y - laid_out.y, 0, 0});
        }
        return {.consumed = true};
    }

    EventResult hover(bool inside) override {
        if (kind_ == Kind::help) {
            if (inside) {
                open(PopupRequest::Kind::tooltip, PopupRequest::Placement::below, drawn_frame());
            } else if (services_) {
                services_->close_tooltip();
            }
        }
        return {};
    }

    EventResult key(const KeyEvent& event) override {
        if (kind_ != Kind::context_menu || event.kind != KeyEvent::Kind::down) {
            return {};
        }
        const bool shift_f10 = event.key == Key::f10 && has(event.modifiers, KeyModifiers::shift);
        if (event.key == Key::menu || shift_f10) {
            const Rect bounds = drawn_frame();
            open(PopupRequest::Kind::menu, PopupRequest::Placement::at_point, {bounds.x, bounds.max_y(), 0, 0});
            return {.consumed = true};
        }
        return {};
    }

    Kind kind_;
    std::unique_ptr<Element> content_;
    std::function<View()> popup_;
    EnvironmentValues environment_;
    WindowServices* services_;
};

class PopupTriggerNode final : public ViewNode {
public:
    PopupTriggerNode(PopupTriggerElement::Kind kind, View content, std::function<View()> popup)
        : kind_(kind), content_(std::move(content)), popup_(std::move(popup)) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        auto element = std::make_unique<PopupTriggerElement>(kind_, detail::lower(content_, context), popup_,
                                                            context.environment, context.services);
        if (context.focus) {
            element->set_focus_manager(context.focus, {}); // lets go of focus if destroyed
        }
        return element;
    }

private:
    PopupTriggerElement::Kind kind_;
    View content_;
    std::function<View()> popup_;
};

// --- Popovers ---

// Wraps a view; opens a popover by it while the binding is true, and closes it when the binding
// goes false. Closed from outside (a click elsewhere, Esc), it sets the binding back to false.
class PopoverElement final : public Element {
public:
    // Shared with the popup's on_close, which may run after this element is gone.
    struct Presence {
        bool open = false;   // the popup was requested and hasn't closed since
        bool wanted = false; // the binding's value, as last seen
        int generation = 0;  // +1 per open and close(), so a stale on_close does nothing
    };

    PopoverElement(std::unique_ptr<Element> content, std::function<View()> popover, PopoverOptions options,
                   Binding<bool> is_presented, EnvironmentValues environment, WindowServices* services)
        : content_(std::move(content)), popover_(std::move(popover)), options_(options),
          is_presented_(std::move(is_presented)), environment_(std::move(environment)), services_(services),
          level_(services ? services->menu_level() + 1 : 0) {
        adopt(*content_);
    }
    ~PopoverElement() override { close(); } // its view went away: so does the popover

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

protected:
    Size measure(ProposedSize proposal) override { return content_->size_that_fits(proposal); }
    void arrange(Rect bounds, ProposedSize proposal) override {
        content_->place({bounds.x, bounds.y}, proposal);
        arranged_ = true;
        if (presence_->wanted) {
            open(); // presented from the start
        }
    }
    void paint_impl(DisplayList& list) const override { content_->paint(list); }
    bool hit_impl(Point point, std::vector<Element*>& path) override { return content_->hit_test(point, path); }

private:
    void open() {
        if (!services_ || presence_->open) {
            return;
        }
        presence_->open = true;
        const int generation = ++presence_->generation;
        PopupRequest::Placement placement = PopupRequest::Placement::below;
        switch (options_.opens) {
        case PopoverEdge::below: placement = PopupRequest::Placement::below; break;
        case PopoverEdge::above: placement = PopupRequest::Placement::above; break;
        case PopoverEdge::trailing: placement = PopupRequest::Placement::beside; break;
        case PopoverEdge::leading: placement = PopupRequest::Placement::leading; break;
        }
        services_->open_popup({
            .kind = PopupRequest::Kind::popover,
            .placement = placement,
            .centered = true,
            .anchor = drawn_frame(),
            .content = [content = popover_, padded = options_.padded] {
                const bool* fixed = padded.static_value();
                const View panel =
                    fixed ? (*fixed ? content().padding(12) : content())
                          : content().padding(Prop<float>(
                                [padded](const EnvironmentValues& env) { return padded.get(env) ? 12.f : 0.f; }));
                return panel.clip_shape(RoundedRectangle(10))
                    .background(ThemeColor::surface, RoundedRectangle(10))
                    .overlay(RoundedRectangle(10).stroke_border(ThemeColor::outline, 1));
            },
            .environment = environment_,
            .level = level_,
            .on_close =
                [presence = presence_, binding = is_presented_, generation] {
                    // Closed from outside (close() moved the generation on): the binding goes
                    // back to false, which then finds nothing left to close.
                    if (presence->generation != generation) {
                        return;
                    }
                    presence->open = false;
                    if (presence->wanted) {
                        presence->wanted = false;
                        binding.set(false);
                    }
                },
        });
    }

    void close() {
        if (services_ && std::exchange(presence_->open, false)) {
            ++presence_->generation;
            services_->close_menus(level_);
        }
    }

    std::unique_ptr<Element> content_;
    std::function<View()> popover_;
    PopoverOptions options_;
    Binding<bool> is_presented_;
    EnvironmentValues environment_;
    WindowServices* services_;
    int level_;
    std::shared_ptr<Presence> presence_ = std::make_shared<Presence>();
    bool arranged_ = false;
};

class PopoverNode final : public ViewNode {
public:
    PopoverNode(View content, Binding<bool> is_presented, std::function<View()> popover, PopoverOptions options)
        : content_(std::move(content)), is_presented_(std::move(is_presented)), popover_(std::move(popover)),
          options_(options) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        auto element = std::make_unique<PopoverElement>(detail::lower(content_, context), popover_, options_,
                                                        is_presented_, context.environment, context.services);
        PopoverElement* const target = element.get();
        detail::bind(Prop<bool>(is_presented_), context, [target](bool presented) { target->set_presented(presented); });
        return element;
    }

private:
    View content_;
    Binding<bool> is_presented_;
    std::function<View()> popover_;
    PopoverOptions options_;
};

// A Button (in the environment's ButtonStyle) whose action opens a menu below it.
class MenuButtonNode final : public ViewNode {
public:
    MenuButtonNode(View label, std::function<View()> menu, MenuButtonOptions options)
        : label_(std::move(label)), menu_(std::move(menu)), options_(options) {}
    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        const State<bool> pressed(false);
        const State<bool> hovered(false);
        const State<bool> focused(false);
        const EnvironmentValues environment = context.environment;
        const ButtonConfiguration configuration{
            .label = If(options_.shows_indicator, HStack({.spacing = 6}, label_, icon(Icon::chevron_down, 12, Weight::bold)),
                        label_),
            .is_pressed = [pressed] { return pressed(); },
            .is_hovered = [hovered] { return hovered(); },
            .is_enabled = [environment] { return !environment.get<DisabledKey>(); },
            .is_focused = [focused] { return focused(); },
        };
        const std::shared_ptr<const ButtonStyle> style = untrack([&] { return environment.get<ButtonStyleKey>(); });
        View body;
        {
            const EnvironmentScope scope(environment);
            body = style->make_body(configuration);
        }
        auto element_slot = std::make_shared<Element*>(nullptr);
        WindowServices* const services = context.services;
        auto element = make_button_element(
            body,
            [services, element_slot, menu = menu_, environment, edge = options_.opens] {
                if (services && *element_slot) {
                    services->open_popup({.kind = PopupRequest::Kind::menu,
                                          .placement = edge == MenuEdge::above ? PopupRequest::Placement::above
                                                                               : PopupRequest::Placement::below,
                                          .anchor = (*element_slot)->drawn_frame(),
                                          .content = menu,
                                          .environment = environment,
                                          .level = services->menu_level() + 1});
                }
            },
            pressed, hovered, context);
        element->set_focus_state(focused);
        *element_slot = element.get();
        AccessibilitySpec semantics;
        semantics.role = AccessRole::popup_button;
        return with_accessibility(std::move(element), std::move(semantics));
    }

private:
    View label_;
    std::function<View()> menu_;
    MenuButtonOptions options_;
};

} // namespace

} // namespace elg::detail

namespace elg {

using detail::ViewAccess;

View MenuItem(Prop<std::string> title, std::function<void()> action, MenuItemOptions options) {
    return ViewAccess::make(
        std::make_shared<const detail::MenuItemNode>(std::move(title), std::move(action), std::move(options)));
}

View Submenu(Prop<std::string> title, std::function<View()> menu) {
    return ViewAccess::make(std::make_shared<const detail::SubmenuNode>(std::move(title), std::move(menu)));
}

View detail::make_menu(std::vector<View> items) {
    // Sized by its popup window, which fits the content; dividers and rows span the width.
    return VStackLayout(HorizontalAlignment::leading, 2)(std::move(items))
        .padding(5)
        .frame({.min_width = 200})
        .background(ThemeColor::surface, RoundedRectangle(9))
        .overlay(RoundedRectangle(9).stroke_border(ThemeColor::outline, 1));
}

View MenuButton(View label, std::function<View()> menu) {
    return MenuButton(std::move(label), std::move(menu), MenuButtonOptions{});
}

View MenuButton(View label, std::function<View()> menu, MenuButtonOptions options) {
    return ViewAccess::make(std::make_shared<const detail::MenuButtonNode>(std::move(label), std::move(menu), options));
}

View MenuButton(Prop<std::string> title, std::function<View()> menu) {
    return MenuButton(Text(std::move(title)), std::move(menu));
}

View View::context_menu(std::function<View()> menu) const {
    return ViewAccess::make(std::make_shared<const detail::PopupTriggerNode>(
        detail::PopupTriggerElement::Kind::context_menu, *this, std::move(menu)));
}

View View::help(Prop<std::string> text) const {
    auto tooltip = [text = std::move(text)] {
        return Text(text)
            .font(Font::system(12))
            .foreground_style(ThemeColor::primary)
            .padding(EdgeInsets::symmetric(8, 5))
            .background(ThemeColor::surface, RoundedRectangle(5))
            .overlay(RoundedRectangle(5).stroke_border(ThemeColor::outline, 1));
    };
    return ViewAccess::make(std::make_shared<const detail::PopupTriggerNode>(detail::PopupTriggerElement::Kind::help,
                                                                            *this, std::move(tooltip)));
}

View View::popover(Binding<bool> is_presented, std::function<View()> content, PopoverOptions options) const {
    return ViewAccess::make(std::make_shared<const detail::PopoverNode>(*this, std::move(is_presented),
                                                                       std::move(content), options));
}

View detail::make_menu_picker(std::vector<std::string> labels, std::function<std::size_t()> selected,
                              std::function<void(std::size_t)> select) {
    const auto shared = std::make_shared<const std::vector<std::string>>(std::move(labels));
    const View current = Text([shared, selected] {
        const std::size_t index = selected();
        return index < shared->size() ? (*shared)[index] : std::string();
    });
    return MenuButton(current, [shared, selected, select] {
        std::vector<View> items;
        for (std::size_t i = 0; i < shared->size(); ++i) {
            items.push_back(MenuItem((*shared)[i], [select, i] { select(i); },
                                     {.checked = [selected, i] { return selected() == i; }}));
        }
        return detail::make_menu(std::move(items));
    }).button_style(BorderedButtonStyle{});
}

} // namespace elg
