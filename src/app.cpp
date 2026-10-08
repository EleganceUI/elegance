// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include <elg/app.h>

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

// A window while the app runs. Member order matters:
// - the surface is destroyed before the window it belongs to;
// - `dirty` and `scope` exist before mounting (the tree refers to them) and the scope outlives
//   the tree, so effects are disposed after the elements they update are gone.
struct LiveWindow {
    LiveWindow(WindowEntry& entry, const gpu::Instance& instance, TextSystem& text, EnvironmentValues base)
        : entry(&entry), window(entry.desc), surface(instance, window.create_surface(instance.handle())),
          environment(std::move(base)), root(mount(text)) {}

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
                                                    .focus = &focus});
    }

    WindowEntry* entry;
    platform::Window window;
    gpu::Surface surface;
    bool dirty = true;
    bool tree_changed = false; // If/ForEach added or removed elements
    bool animating = false;    // the last frame had animations in progress
    std::uint64_t last_frame = 0;
    EnvironmentValues environment; // the root environment (system color scheme)
    FocusManager focus;            // outlives the tree (elements let go of it when destroyed)
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
    frame.list.clear_color = live.entry->desc.background.value_or(
        resolve_theme_color(live.environment, ThemeColor::background));
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

    // Fonts and glyphs, shared by all windows; outlives every element tree.
    TextSystem text;

    // Every window's environment starts from the system's light/dark setting, as reactive state:
    // when it changes, only the colors that depend on it update.
    const auto system_scheme = [] { return platform::system_prefers_dark() ? ColorScheme::dark : ColorScheme::light; };
    const State<ColorScheme> scheme(system_scheme());
    EnvironmentValues base;
    base.set<ColorSchemeKey>(scheme);

    std::vector<std::unique_ptr<LiveWindow>> windows;
    for (WindowEntry& entry : self.windows) {
        windows.push_back(std::make_unique<LiveWindow>(entry, instance, text, base));
    }
    const auto find_window = [&](std::uint32_t id) {
        return std::ranges::find_if(windows, [&](const auto& live) { return live->window.id() == id; });
    };

    FrameMailbox mailbox;
    RenderThread renderer(instance, windows.front()->surface.handle(), mailbox,
                          [&platform] { platform.wake(); });
    for (const auto& live : windows) {
        mailbox.post(AddWindowCommand{live->window.id(), live->surface.handle()});
    }

    // Builds and posts a window's frame. Glyphs it used for the first time go to the atlas first.
    const auto post_frame = [&](LiveWindow& live) {
        WindowFrame frame = build_frame(live);
        live.dirty = false;
        live.animating = frame.list.animating;
        if (std::vector<GlyphUpload> uploads = text.atlas.take_uploads(); !uploads.empty()) {
            mailbox.post(AtlasUploadCommand{std::move(uploads)});
        }
        live.last_frame = mailbox.post(std::move(frame));
        return live.last_frame;
    };

    // Draw before showing, so windows never appear blank.
    std::uint64_t first_frame = 0;
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
            }
            break;
        case platform::EventType::window_close_requested:
            log_debug("window {} close requested", event.window_id);
            if (auto it = find_window(event.window_id); it != windows.end()) {
                if (windows.size() == 1) {
                    self.quit_requested = true;
                } else {
                    // The render thread must release the swapchain before the surface goes.
                    mailbox.wait_until_done(mailbox.post(RemoveWindowCommand{event.window_id}));
                    windows.erase(it);
                }
            }
            break;
        case platform::EventType::pointer_move:
        case platform::EventType::pointer_down:
        case platform::EventType::pointer_up:
        case platform::EventType::pointer_leave:
            if (auto it = find_window(event.window_id); it != windows.end()) {
                LiveWindow& live = **it;
                const Point location = live.window.to_logical(event.x, event.y);
                bool redraw = false;
                switch (event.type) {
                case platform::EventType::pointer_move: redraw = live.events.pointer_moved(location); break;
                case platform::EventType::pointer_down:
                    redraw = live.events.pointer_button(PointerEvent::Kind::down, location,
                                                        to_mouse_button(event.button), event.clicks);
                    break;
                case platform::EventType::pointer_up:
                    redraw = live.events.pointer_button(PointerEvent::Kind::up, location,
                                                        to_mouse_button(event.button), event.clicks);
                    break;
                default: redraw = live.events.pointer_left(); break;
                }
                live.dirty |= redraw;
            }
            break;
        case platform::EventType::wheel:
            if (auto it = find_window(event.window_id); it != windows.end()) {
                constexpr float kPointsPerNotch = 48;
                LiveWindow& live = **it;
                live.dirty |= live.events.scroll(live.window.to_logical(event.x, event.y),
                                                 event.wheel_x * kPointsPerNotch, -event.wheel_y * kPointsPerNotch,
                                                 event.precise);
            }
            break;
        case platform::EventType::key_down:
        case platform::EventType::key_up:
            if (auto it = find_window(event.window_id); it != windows.end()) {
                const KeyEvent key{event.type == platform::EventType::key_down ? KeyEvent::Kind::down : KeyEvent::Kind::up,
                                   event.key, event.modifiers, event.repeat};
                (*it)->dirty |= (*it)->events.key(key);
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
        if (!animating) {
            // Wake for the next pending hover change (hover delays), else sleep until an event.
            std::optional<double> deadline;
            for (const auto& live : windows) {
                if (const std::optional<double> due = live->events.next_deadline(); due && (!deadline || *due < *deadline)) {
                    deadline = due;
                }
            }
            handle(deadline ? platform.wait_event(*deadline - seconds_now()) : platform.wait_event());
        }
        while (auto event = platform.poll_event()) {
            handle(*event);
        }
        for (const auto& live : windows) {
            live->dirty |= live->events.tick(); // hover changes whose delay has passed
        }
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
                live->dirty |= live->events.revalidate();
            }
            if (live->dirty || live->animating) {
                post_frame(*live);
                // Content may have moved under the pointer (scrolling, animations).
                live->dirty |= live->events.refresh_hover();
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
