// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include <elg/app.h>
#include <elg/window.h>

#include "core/crash.h"
#include "core/log.h"
#include "core/scope_exit.h"
#include "gpu/instance.h"
#include "platform/platform.h"
#include "reactive/runtime.h"
#include "render/frame_mailbox.h"
#include "render/render_thread.h"
#include "text/text_system.h"
#include "view/animated.h"
#include "view/event_dispatcher.h"
#include "view/view_node.h"

#include <elg/error.h>
#include <elg/animation.h>
#include <elg/environment.h>
#include <elg/reactive.h>

#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <thread>
#include <vector>

namespace elg {

using namespace detail;
using namespace std::chrono_literals;

namespace {

// How long the UI thread waits for a frame to be presented when it must be on screen before
// continuing (first frame, live resize). Bounded so a stuck driver can't freeze the app.
constexpr auto kPresentTimeout = 100ms;
constexpr auto kFirstFrameTimeout = 1000ms;
// How long a new window (or popup) waits for its glyphs before showing anyway.
constexpr auto kGlyphWarmup = 1500ms;
constexpr auto kPopupGlyphWarmup = 150ms;
// While animating, the loop paces itself by waiting for the previous frame's present (vsync);
// this bounds the wait if presentation stalls.
constexpr auto kAnimationFrameTimeout = 50ms;

double seconds_now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

struct WindowEntry {
    WindowDesc desc;
    ContentFunction content;
};

// Where popup requests go: the app's popup host (in App::run).
struct PopupSink {
    std::function<void(PopupRequest, std::uint32_t opener)> open;
    std::function<void(int level)> close_menus;
    std::function<void(std::function<void()>)> close_menus_then;
    std::function<void()> close_tooltip;
};

// What a window's elements need from the platform beyond drawing (view/window_services.h).
class LiveWindowServices final : public WindowServices {
public:
    explicit LiveWindowServices(platform::Window& window) noexcept : window_(&window) {}

    void set_popups(const PopupSink* popups, int menu_level) noexcept {
        popups_ = popups;
        menu_level_ = menu_level;
    }
    void open_popup(PopupRequest request) override {
        if (popups_) {
            popups_->open(std::move(request), window_->id());
        }
    }
    void close_menus(int level) override {
        if (popups_) {
            popups_->close_menus(level);
        }
    }
    void close_menus_then(std::function<void()> action) override {
        if (popups_) {
            popups_->close_menus_then(std::move(action));
        } else if (action) {
            action();
        }
    }
    void close_tooltip() override {
        if (popups_) {
            popups_->close_tooltip();
        }
    }
    int menu_level() const override { return menu_level_; }

    void start_text_input() override { window_->start_text_input(); }
    void stop_text_input() override { window_->stop_text_input(); }
    void set_text_input_area(Rect area, float cursor) override { window_->set_text_input_area(area, cursor); }
    std::string clipboard_text() override { return platform::clipboard_text(); }
    void set_clipboard_text(const std::string& text) override { platform::set_clipboard_text(text); }
    void set_cursor(CursorShape shape) override { platform::set_text_cursor(shape == CursorShape::text); }
    void wake_at(double seconds) override {
        if (!wake_ || seconds < *wake_) {
            wake_ = seconds;
        }
    }

    std::optional<double> wake() const noexcept { return wake_; }
    // True (and forgets it) when the requested wake time has come.
    bool take_due(double now) noexcept {
        if (wake_ && *wake_ <= now) {
            wake_.reset();
            return true;
        }
        return false;
    }

private:
    platform::Window* window_;
    std::optional<double> wake_;
    const PopupSink* popups_ = nullptr;
    int menu_level_ = -1;
};

} // namespace

// What a WindowHandle refers to; outlives the window (handles may be kept), so `window` is
// cleared when it closes.
namespace detail {
struct WindowState {
    platform::Window* window = nullptr;
    State<bool> maximized{false};
    State<bool> active{true};
};
} // namespace detail

namespace {

EnvironmentValues with_window(EnvironmentValues environment, const std::shared_ptr<WindowState>& state) {
    environment.set<WindowKey>(WindowHandle(state));
    return environment;
}

// A window while the app runs. Member order matters:
// - the surface is destroyed before the window it belongs to;
// - `dirty` and `scope` exist before mounting (the tree refers to them) and the scope outlives
//   the tree, so effects are disposed after the elements they update are gone.
struct LiveWindow {
    LiveWindow(WindowEntry& entry, const gpu::Instance& instance, TextSystem& text, EnvironmentValues base,
               const PopupSink* popups, const platform::PopupSpec* popup = nullptr, int menu_level = -1)
        : entry(&entry), window(entry.desc, popup), is_popup(popup != nullptr), surface(instance, window.create_surface(instance.handle())),
          state(std::make_shared<WindowState>(WindowState{.window = &window})),
          environment(with_window(std::move(base), state)), services(make_services(window, popups, menu_level)),
          root(mount(text)) {
        if (entry.desc.title_bar == TitleBar::hidden) {
            window.set_hit_test([this](Point point) { return hit_area(point); });
        }
    }

    ~LiveWindow() { state->window = nullptr; } // handles kept by the app now do nothing

    // Undecorated windows: resize at the edges, move from .window_drag_area() views.
    platform::HitArea hit_area(Point point) {
        if (entry->desc.resizable && !window.maximized()) {
            constexpr float kEdge = 6;
            const Size size = window.logical_size();
            const bool left = point.x < kEdge;
            const bool right = point.x >= size.width - kEdge;
            const bool top = point.y < kEdge;
            const bool bottom = point.y >= size.height - kEdge;
            if (top && left) {
                return platform::HitArea::resize_top_left;
            }
            if (top && right) {
                return platform::HitArea::resize_top_right;
            }
            if (bottom && left) {
                return platform::HitArea::resize_bottom_left;
            }
            if (bottom && right) {
                return platform::HitArea::resize_bottom_right;
            }
            if (top) {
                return platform::HitArea::resize_top;
            }
            if (bottom) {
                return platform::HitArea::resize_bottom;
            }
            if (left) {
                return platform::HitArea::resize_left;
            }
            if (right) {
                return platform::HitArea::resize_right;
            }
        }
        return events.drags_window_at(point) ? platform::HitArea::drag : platform::HitArea::normal;
    }

    // Builds the element tree. The body runs once (design.md §6); effects it creates belong to
    // this window's scope.
    std::unique_ptr<Element> mount(TextSystem& text) {
        Scope::Use use(scope);
        const EnvironmentScope environment_scope(environment); // Environment<K>() in the content function
        View content = entry->content();
        return lower(content, LowerContext{.text = &text,
                                                    .needs_frame = &dirty,
                                                    .tree_changed = &tree_changed,
                                                    .environment = environment,
                                                    .focus = &focus,
                                                    .services = &services});
    }

    static LiveWindowServices make_services(platform::Window& window, const PopupSink* popups, int menu_level) {
        LiveWindowServices services(window);
        services.set_popups(popups, menu_level);
        return services;
    }

    WindowEntry* entry;
    platform::Window window;
    bool is_popup;
    gpu::Surface surface;
    std::shared_ptr<WindowState> state; // behind this window's WindowHandles
    bool dirty = true;
    bool tree_changed = false; // If/ForEach added or removed elements
    bool animating = false;    // the last frame had animations in progress
    std::uint64_t last_frame = 0;
    EnvironmentValues environment; // the root environment (system color scheme)
    LiveWindowServices services;
    FocusManager focus{&services}; // outlives the tree (elements let go of it when destroyed)
    Scope scope;
    std::unique_ptr<Element> root;
    EventDispatcher events{*root, focus};
};

MouseButton to_mouse_button(platform::PointerButton button) {
    switch (button) {
    case platform::PointerButton::left: return MouseButton::left;
    case platform::PointerButton::middle: return MouseButton::middle;
    case platform::PointerButton::right: return MouseButton::right;
    case platform::PointerButton::other: return MouseButton::other;
    }
    return MouseButton::other;
}

// Lays out the root view in the window (centered, like SwiftUI) and paints it.
WindowFrame build_frame(LiveWindow& live) {
    WindowFrame frame;
    frame.window_id = live.window.id();
    const platform::PixelSize pixels = live.window.pixel_size();
    frame.pixel_width = pixels.width;
    frame.pixel_height = pixels.height;
    frame.scale = live.window.scale();
    // Transparent windows start clear, so only what's drawn covers the desktop.
    frame.list.clear_color = live.entry->desc.background.value_or(
        live.entry->desc.transparent ? Color::clear : resolve_theme_color(live.environment, ThemeColor::background));
    frame.list.scale = frame.scale;

    const Size size = live.window.logical_size();
    const ProposedSize proposal{size.width, size.height};
    const Size fitted = live.root->size_that_fits(proposal);
    live.root->place(aligned_position({0, 0, size.width, size.height}, fitted, Alignment::center), proposal);
    live.root->paint(frame.list);
    return frame;
}

} // namespace

struct App::Impl {
    std::vector<WindowEntry> windows;
    bool running = false;
    bool quit_requested = false;
    platform::Platform* platform = nullptr; // valid while running
};

App::App() : impl_(std::make_unique<Impl>()) {}

void WindowHandle::close() const {
    if (state_ && state_->window) {
        state_->window->request_close();
    }
}

void WindowHandle::minimize() const {
    if (state_ && state_->window) {
        state_->window->minimize();
    }
}

void WindowHandle::toggle_maximized() const {
    if (state_ && state_->window) {
        state_->window->toggle_maximized();
    }
}

bool WindowHandle::is_maximized() const {
    return state_ && state_->maximized();
}

bool WindowHandle::is_active() const {
    return !state_ || state_->active();
}

App::~App() = default;

App& App::window(WindowDesc desc, ContentFunction content) {
    if (impl_->running) {
        throw UsageError("App::window(): adding windows while the app runs isn't supported yet");
    }
    if (!content) {
        throw UsageError("App::window(): the content function is empty");
    }
    impl_->windows.push_back({std::move(desc), std::move(content)});
    return *this;
}

void App::quit() {
    impl_->quit_requested = true;
    if (impl_->platform) {
        impl_->platform->wake();
    }
}

int App::run() {
    Impl& self = *impl_;
    if (self.running) {
        throw UsageError("App::run() is already running");
    }
    if (self.windows.empty()) {
        throw UsageError("App::run(): no windows; call App::window() first");
    }
    self.running = true;
    self.quit_requested = false;
    ScopeExit reset([&] {
        self.running = false;
        self.platform = nullptr;
    });

    // Reactive state belongs to this thread while the app runs (design.md §6).
    set_ui_thread(std::this_thread::get_id());
    ScopeExit release_ui_thread([] { set_ui_thread(std::nullopt); });

    // Declaration order is teardown order in reverse: the render thread stops first, then
    // surfaces and windows go, then the Vulkan instance, then SDL.
    CrashHandlerScope crash_handlers;
    platform::Platform platform;
    self.platform = &platform;
    const std::vector<const char*> extensions = platform.vulkan_instance_extensions();
    gpu::Instance instance(extensions);

    // Fonts and glyphs, shared by all windows; outlives every element tree. Glyphs finishing on
    // the atlas's workers wake the event loop to draw them.
    TextSystem text;
    text.atlas.set_on_ready([&platform] { platform.wake(); });
    ScopeExit stop_glyph_wakes([&text] { text.atlas.set_on_ready({}); });

    // Every window's environment starts from the system's light/dark setting, as reactive state:
    // when it changes, only the colors that depend on it update.
    const auto system_scheme = [] { return platform::system_prefers_dark() ? ColorScheme::dark : ColorScheme::light; };
    const State<ColorScheme> scheme(system_scheme());
    EnvironmentValues base;
    base.set<ColorSchemeKey>(scheme);

    // Popups (design.md §13.1): menus (a stack: level 0, then submenus) and one tooltip, each a
    // popup window with its own tree. Requests come from event handlers and effects and are
    // carried out between events and layout, never while an event is being handled.
    struct OpenPopup {
        std::unique_ptr<WindowEntry> entry;
        std::uint32_t id = 0;
        int level = 0; // menus
    };
    std::vector<OpenPopup> menus;
    std::optional<OpenPopup> tooltip;
    struct PendingPopups {
        std::optional<int> close_level;
        bool close_tooltip = false;
        std::vector<std::function<void()>> after_close;
        std::vector<std::pair<PopupRequest, std::uint32_t>> opens;
        bool any() const { return close_level || close_tooltip || !after_close.empty() || !opens.empty(); }
    } pending;
    const auto request_close = [&](int level) {
        pending.close_level = pending.close_level ? std::min(*pending.close_level, level) : level;
        // Opening a deeper menu after this request would be pointless.
        std::erase_if(pending.opens, [&](const auto& open) {
            return open.first.kind == PopupRequest::Kind::menu && open.first.level >= level;
        });
    };
    const PopupSink popup_sink{
        .open =
            [&](PopupRequest request, std::uint32_t opener) {
                if (request.kind == PopupRequest::Kind::menu) {
                    request_close(request.level);
                } else {
                    pending.close_tooltip = true;
                }
                pending.opens.emplace_back(std::move(request), opener);
            },
        .close_menus = [&](int level) { request_close(level); },
        .close_menus_then =
            [&](std::function<void()> action) {
                request_close(0);
                if (action) {
                    pending.after_close.push_back(std::move(action));
                }
            },
        .close_tooltip = [&] { pending.close_tooltip = true; },
    };

    std::vector<std::unique_ptr<LiveWindow>> windows;
    for (WindowEntry& entry : self.windows) {
        windows.push_back(std::make_unique<LiveWindow>(entry, instance, text, base, &popup_sink));
    }
    const auto find_window = [&](std::uint32_t id) {
        return std::ranges::find_if(windows, [&](const auto& live) { return live->window.id() == id; });
    };

    FrameMailbox mailbox;
    RenderThread renderer(instance, windows.front()->surface.handle(), mailbox,
                          [&platform] { platform.wake(); });
    for (const auto& live : windows) {
        mailbox.post(AddWindowCommand{live->window.id(), live->surface.handle(), live->entry->desc.transparent,
                                      live->window.native_handle()});
    }

    // Builds and posts a window's frame. Glyphs it used for the first time go to the atlas first.
    const auto post_frame = [&](LiveWindow& live) {
        // Cleared first: layout may ask for another frame (lazy stacks measuring new items,
        // ScrollPosition requests settling), and that request must survive.
        live.dirty = false;
        WindowFrame frame = build_frame(live);
        if (std::exchange(live.tree_changed, false)) {
            if (live.events.revalidate()) { // layout added or removed elements (lazy stacks)
                live.dirty = true;
            }
        }
        live.animating = frame.list.animating;
        if (std::vector<GlyphUpload> uploads = text.atlas.take_uploads(); !uploads.empty()) {
            mailbox.post(AtlasUploadCommand{std::move(uploads)});
        }
        live.last_frame = mailbox.post(std::move(frame));
        return live.last_frame;
    };

    // Draw before showing, so windows never appear blank: lay out once (which requests the
    // glyphs), let the glyph workers finish (briefly), then draw with them.
    std::uint64_t first_frame = 0;
    for (const auto& live : windows) {
        first_frame = post_frame(*live);
    }
    if (!text.atlas.wait_idle(kGlyphWarmup)) {
        log_debug("showing windows before all glyphs are ready");
    }
    for (const auto& live : windows) {
        first_frame = post_frame(*live);
    }
    mailbox.wait_until_done(first_frame, kFirstFrameTimeout);
    for (const auto& live : windows) {
        live->window.show();
    }

    // While the user drags a window edge, the OS blocks the event loop below in a modal loop.
    // SDL calls this from inside it, so the window keeps rendering at its new size. Waiting for
    // the present keeps the OS from stretching a stale frame.
    std::exception_ptr callback_error;
    platform.set_live_resize_callback([&](std::uint32_t window_id) {
        if (callback_error) {
            return;
        }
        try {
            if (auto it = find_window(window_id); it != windows.end()) {
                set_frame_time(seconds_now());
                mailbox.wait_until_done(post_frame(**it), kPresentTimeout);
            }
        } catch (...) {
            // Rethrown from the main loop; nothing may unwind through the OS modal loop.
            callback_error = std::current_exception();
            platform.wake();
        }
    });
    ScopeExit clear_callback([&] { platform.set_live_resize_callback({}); });

    // --- Popups ---

    const auto destroy_window = [&](std::uint32_t id) {
        if (auto it = find_window(id); it != windows.end()) {
            // The render thread must release the swapchain before the surface goes.
            mailbox.wait_until_done(mailbox.post(RemoveWindowCommand{id}));
            windows.erase(it);
        }
    };
    const auto close_menus_from = [&](int level) {
        // Deepest first: a submenu is a child window of the menu that opened it.
        while (!menus.empty() && menus.back().level >= level) {
            destroy_window(menus.back().id);
            menus.pop_back();
        }
    };
    const auto close_tooltip_now = [&] {
        if (tooltip) {
            destroy_window(tooltip->id);
            tooltip.reset();
        }
    };
    const auto top_menu = [&]() -> LiveWindow* {
        if (menus.empty()) {
            return nullptr;
        }
        auto it = find_window(menus.back().id);
        return it != windows.end() ? it->get() : nullptr;
    };

    // Room around a popup's panel for its shadow; the window is that much bigger.
    constexpr float kPopupMargin = 14;
    const auto open_popup = [&](PopupRequest request, std::uint32_t opener_id) {
        auto opener_it = find_window(opener_id);
        if (opener_it == windows.end()) {
            return; // closed meanwhile
        }
        LiveWindow& opener = **opener_it;
        const bool is_menu = request.kind == PopupRequest::Kind::menu;
        auto entry = std::make_unique<WindowEntry>();
        entry->desc = {.title = "", .width = 1, .height = 1, .resizable = false,
                       .title_bar = TitleBar::hidden, .transparent = true};
        const float shadow = is_menu ? 12.f : 6.f;
        entry->content = [content = request.content, shadow] {
            return content().shadow({.color = Color::black.opacity(0.28f), .radius = shadow, .y = 3}).padding(kPopupMargin);
        };
        const platform::PopupSpec spec{.parent = &opener.window, .tooltip = !is_menu};
        // Menu rows respond to the pointer at once, like native menus (submenus have their own
        // opening delay).
        EnvironmentValues environment = request.environment;
        if (is_menu) {
            environment.set<HoverDelayKey>(HoverDelay{.enter = 0, .exit = 0});
        }
        auto live = std::make_unique<LiveWindow>(*entry, instance, text, environment, &popup_sink, &spec,
                                                 is_menu ? request.level : -1);
        live->events.set_menu_navigation(is_menu);

        // Size to the content; place the panel by the anchor, kept on the display.
        const Size size = live->root->size_that_fits({});
        const Size panel{size.width - 2 * kPopupMargin, size.height - 2 * kPopupMargin};
        const Rect anchor = request.anchor;
        const Rect area = opener.window.usable_area();
        Point origin;
        switch (request.placement) {
        case PopupRequest::Placement::at_point:
            origin = {anchor.x, anchor.y};
            if (origin.x + panel.width > area.max_x()) {
                origin.x = anchor.x - panel.width; // open leftwards
            }
            if (origin.y + panel.height > area.max_y()) {
                origin.y = anchor.y - panel.height; // open upwards
            }
            break;
        case PopupRequest::Placement::below:
            origin = {anchor.x, anchor.max_y() + 4};
            if (origin.y + panel.height > area.max_y()) {
                origin.y = anchor.y - 4 - panel.height; // above instead
            }
            break;
        case PopupRequest::Placement::beside:
            origin = {anchor.max_x() - 2, anchor.y - 4};
            if (origin.x + panel.width > area.max_x()) {
                origin.x = anchor.x - panel.width + 2; // to the left instead
            }
            break;
        }
        origin.x = std::clamp(origin.x, area.x, std::max(area.x, area.max_x() - panel.width));
        origin.y = std::clamp(origin.y, area.y, std::max(area.y, area.max_y() - panel.height));
        live->window.set_popup_frame({origin.x - kPopupMargin, origin.y - kPopupMargin, size.width, size.height});

        mailbox.post(AddWindowCommand{live->window.id(), live->surface.handle(), true, live->window.native_handle()});
        post_frame(*live); // requests its glyphs
        text.atlas.wait_idle(kPopupGlyphWarmup);
        mailbox.wait_until_done(post_frame(*live), kFirstFrameTimeout);
        live->window.show();
        const std::uint32_t id = live->window.id();
        windows.push_back(std::move(live));
        if (is_menu) {
            menus.push_back({std::move(entry), id, request.level});
        } else {
            tooltip = OpenPopup{std::move(entry), id, 0};
        }
    };

    // Carries out queued requests: closes, then actions of chosen items, then opens.
    bool check_popup_focus = false;
    const auto process_popups = [&] {
        while (pending.any()) {
            if (const std::optional<int> level = std::exchange(pending.close_level, std::nullopt)) {
                close_menus_from(*level);
            }
            if (std::exchange(pending.close_tooltip, false)) {
                close_tooltip_now();
            }
            for (auto& action : std::exchange(pending.after_close, {})) {
                action();
            }
            for (auto& [request, opener] : std::exchange(pending.opens, {})) {
                open_popup(std::move(request), opener);
            }
        }
        // Focus moved to another app: menus close.
        if (std::exchange(check_popup_focus, false) && !menus.empty() && !platform::any_window_focused()) {
            close_menus_from(0);
        }
    };

    const auto handle = [&](const platform::Event& event) {
        // Handlers may start animations (e.g. scrolling); they must start now, not at the last
        // frame's time, which can be long ago after idling.
        set_frame_time(seconds_now());
        switch (event.type) {
        case platform::EventType::quit:
            log_debug("quit requested by the OS");
            self.quit_requested = true;
            break;
        case platform::EventType::window_exposed:
        case platform::EventType::window_resized:
        case platform::EventType::window_scale_changed:
            if (auto it = find_window(event.window_id); it != windows.end()) {
                (*it)->dirty = true;
                if (event.type == platform::EventType::window_resized && !(*it)->is_popup && !menus.empty()) {
                    request_close(0);
                }
            }
            break;
        case platform::EventType::window_close_requested:
            log_debug("window {} close requested", event.window_id);
            if (auto it = find_window(event.window_id); it != windows.end() && !(*it)->is_popup) {
                // Popups belong to a window; destroying it would destroy them under us.
                close_menus_from(0);
                close_tooltip_now();
                const auto regular = std::ranges::count_if(windows, [](const auto& live) { return !live->is_popup; });
                if (regular == 1) {
                    self.quit_requested = true;
                } else {
                    destroy_window(event.window_id);
                }
            }
            break;
        case platform::EventType::pointer_move:
        case platform::EventType::pointer_down:
        case platform::EventType::pointer_up:
        case platform::EventType::pointer_leave:
            if (event.type == platform::EventType::pointer_down) {
                pending.close_tooltip = pending.close_tooltip || tooltip.has_value();
            }
            if (auto it = find_window(event.window_id); it != windows.end()) {
                LiveWindow& live = **it;
                // While a menu is open it has the pointer: elsewhere, a press closes the menus
                // (and isn't passed on), and nothing else reacts.
                if (!menus.empty() && !live.is_popup) {
                    if (event.type == platform::EventType::pointer_down) {
                        request_close(0);
                    }
                    break;
                }
                const Point location = live.window.to_logical(event.x, event.y);
                bool redraw = false;
                switch (event.type) {
                case platform::EventType::pointer_move: redraw = live.events.pointer_moved(location); break;
                case platform::EventType::pointer_down:
                    redraw = live.events.pointer_button(PointerEvent::Kind::down, location,
                                                        to_mouse_button(event.button), event.clicks,
                                                        event.modifiers);
                    break;
                case platform::EventType::pointer_up:
                    redraw = live.events.pointer_button(PointerEvent::Kind::up, location,
                                                        to_mouse_button(event.button), event.clicks,
                                                        event.modifiers);
                    break;
                default: redraw = live.events.pointer_left(); break;
                }
                live.dirty |= redraw;
            }
            break;
        case platform::EventType::wheel:
            pending.close_tooltip = pending.close_tooltip || tooltip.has_value();
            if (auto it = find_window(event.window_id); it != windows.end()) {
                constexpr float kPointsPerNotch = 48;
                LiveWindow& live = **it;
                if (live.events.scroll(live.window.to_logical(event.x, event.y),
                                                 event.wheel_x * kPointsPerNotch, -event.wheel_y * kPointsPerNotch,
                                                 event.precise)) {
                    live.dirty = true;
                }
            }
            break;
        case platform::EventType::text_input:
        case platform::EventType::text_editing:
            if (auto it = find_window(event.window_id); it != windows.end()) {
                const TextInputEvent input{event.type == platform::EventType::text_input
                                               ? TextInputEvent::Kind::commit
                                               : TextInputEvent::Kind::composition,
                                           event.text, event.text_cursor};
                if ((*it)->events.text_input(input)) {
                    (*it)->dirty = true;
                }
            }
            break;
        case platform::EventType::key_down:
        case platform::EventType::key_up: {
            pending.close_tooltip = pending.close_tooltip || tooltip.has_value();
            const KeyEvent key{event.type == platform::EventType::key_down ? KeyEvent::Kind::down : KeyEvent::Kind::up,
                               event.key, event.modifiers, event.repeat};
            if (LiveWindow* menu = top_menu()) {
                // The open menu takes the keyboard: Esc closes it, Left leaves a submenu.
                if (menu->events.key(key)) {
                    menu->dirty = true;
                }
                if (key.kind == KeyEvent::Kind::down) {
                    const int level = menus.back().level;
                    if (key.key == Key::escape || (key.key == Key::left && level > 0)) {
                        request_close(level);
                    }
                }
            } else if (auto it = find_window(event.window_id); it != windows.end()) {
                if ((*it)->events.key(key)) {
                    (*it)->dirty = true;
                }
            }
            break;
        }
        case platform::EventType::window_maximized:
        case platform::EventType::window_restored:
        case platform::EventType::window_focus_gained:
        case platform::EventType::window_focus_lost:
            if (auto it = find_window(event.window_id); it != windows.end()) {
                WindowState& state = *(*it)->state;
                switch (event.type) {
                case platform::EventType::window_maximized: state.maximized = true; break;
                case platform::EventType::window_restored: state.maximized = false; break;
                case platform::EventType::window_focus_gained: state.active = true; break;
                default: state.active = false; break;
                }
            }
            check_popup_focus = check_popup_focus || event.type == platform::EventType::window_focus_lost;
            break;
        case platform::EventType::window_moved:
            // Menus stay where they opened; moving their window closes them.
            if (auto it = find_window(event.window_id); it != windows.end() && !(*it)->is_popup && !menus.empty()) {
                request_close(0);
            }
            break;
        case platform::EventType::system_theme_changed:
            scheme = system_scheme();
            for (const auto& live : windows) {
                live->dirty = true; // the default window background follows the scheme
            }
            break;
        case platform::EventType::wake:
        case platform::EventType::none:
            break;
        }
    };

    set_frame_time(seconds_now());
    while (!self.quit_requested) {
        // Idle: block until something happens. Animating: keep drawing, picking up events as
        // they come.
        const bool animating = std::ranges::any_of(windows, [](const auto& live) { return live->animating; });
        if (!animating && !pending.any()) {
            // Wake for the next pending hover change (hover delays), else sleep until an event.
            std::optional<double> deadline;
            for (const auto& live : windows) {
                for (const std::optional<double> due : {live->events.next_deadline(), live->services.wake()}) {
                    if (due && (!deadline || *due < *deadline)) {
                        deadline = due;
                    }
                }
            }
            handle(deadline ? platform.wait_event(*deadline - seconds_now()) : platform.wait_event());
        }
        while (auto event = platform.poll_event()) {
            handle(*event);
        }
        if (text.atlas.has_uploads()) {
            for (const auto& live : windows) {
                live->dirty = true; // glyphs finished on the workers: draw them
            }
        }
        for (const auto& live : windows) {
            if (live->events.tick()) { // hover changes whose delay has passed
                live->dirty = true;
            }
            if (live->services.take_due(seconds_now())) { // e.g. the caret blinking
                live->dirty = true;
            }
        }
        flush_effects(); // hover and other effects may ask for popups
        process_popups();
        if (callback_error) {
            std::rethrow_exception(callback_error);
        }
        if (std::exception_ptr error = renderer.error()) {
            std::rethrow_exception(error);
        }
        if (self.quit_requested) {
            break;
        }
        // State written by event handlers takes effect here: effects update the elements they
        // bind and request frames, then dirty windows draw. Every animation in a frame samples the
        // same time; layout changes caused by an animated transaction animate too.
        set_frame_time(seconds_now());
        flush_effects();
        const std::shared_ptr<const Animation> transaction = take_flush_animation();
        const LayoutAnimationScope layout_animation(transaction.get());
        for (const auto& live : windows) {
            if (std::exchange(live->tree_changed, false)) {
                if (live->events.revalidate()) {
                    live->dirty = true;
                }
            }
            if (live->dirty || live->animating) {
                post_frame(*live);
                // Content may have moved under the pointer (scrolling, animations).
                if (live->events.refresh_hover()) {
                    live->dirty = true;
                }
            }
        }
        for (const auto& live : windows) {
            if (live->animating) {
                mailbox.wait_until_done(live->last_frame, kAnimationFrameTimeout);
            }
        }
    }
    return 0;
}

const char* version() {
    return ELG_VERSION_STRING;
}

} // namespace elg
