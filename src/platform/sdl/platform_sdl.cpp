// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The platform layer on SDL, the same on every OS. What differs is in platform/<os>/native.*
// (platform/sdl/native.h).

#include "platform/platform.h"

#include "core/crash.h"
#include "core/numeric.h"
#include "core/utf8.h"
#include "platform/sdl/native.h"

#include <elg/error.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <deque>
#include <memory>
#include <cmath>
#include <format>
#include <utility>

namespace elg::detail::platform {

// The platform interface's windows and event loop on SDL.
class SdlWindow final : public Window {
public:
    SdlWindow(const WindowDesc& desc, const PopupSpec* popup);
    ~SdlWindow() override;

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;
    SdlWindow(SdlWindow&&) = delete;
    SdlWindow& operator=(SdlWindow&&) = delete;

    [[nodiscard]] std::uint32_t id() const noexcept override { return id_; }
    [[nodiscard]] PixelSize pixel_size() const override;
    [[nodiscard]] float scale() const override;
    [[nodiscard]] Size logical_size() const override;
    [[nodiscard]] Point to_logical(float x, float y) const override;
    void start_text_input(bool secure) override;
    void stop_text_input() override;
    void update_secure_input() override;
    void set_text_input_area(Rect area, float cursor) override;
    void show() override;
    [[nodiscard]] void* native_handle() const override;
    void minimize() override;
    void toggle_maximized() override;
    [[nodiscard]] bool maximized() const override;
    [[nodiscard]] bool visible() const override;
    void request_close() const override;
    Rect system_controls_frame() override;
    void set_hit_test(std::function<HitArea(Point)> test) override;
    void set_popup_frame(Rect frame) override;
    [[nodiscard]] Rect usable_area() const override;
    [[nodiscard]] HitArea hit_test_at(Point point) const override { return hit_test_ ? hit_test_(point) : HitArea::normal; }
    void refresh_appearance() override;
    [[nodiscard]] SurfaceSource surface_source() const override;

private:
    SDL_Window* window_ = nullptr;
    native::WindowParts native_;
    // Popups: the window they belong to, which has the keyboard (they're never activated), and
    // whether text input was started there for the popup.
    SDL_Window* parent_ = nullptr;
    bool parent_text_input_ = false;
    bool secure_text_input_ = false; // a password field has the text input
    bool secure_engaged_ = false;    // ...and the keyboard, so this window holds Secure Event Input
    std::uint32_t id_ = 0;
    std::function<HitArea(Point)> hit_test_;
};

class SdlPlatform final : public Platform {
public:
    SdlPlatform();
    ~SdlPlatform() override;

    SdlPlatform(const SdlPlatform&) = delete;
    SdlPlatform& operator=(const SdlPlatform&) = delete;
    SdlPlatform(SdlPlatform&&) = delete;
    SdlPlatform& operator=(SdlPlatform&&) = delete;

    Event wait_event() override;
    Event wait_event(double seconds) override;
    std::optional<Event> poll_event() override;
    void wake() const override;
    void post_key(std::uint32_t window_id, Key key, KeyModifiers modifiers) override;
    void set_live_resize_callback(LiveResizeCallback callback) override;
    // From the event watch, inside the OS's modal resize loop.
    void dispatch_live_resize(std::uint32_t window_id) noexcept;

private:
    Event translate(const SDL_Event& event);

    // The UI thread's set-up for the run (native::begin_run()): made first and undone last, around
    // SDL's whole life, and undone too if the constructor throws.
    class RunSetup {
    public:
        RunSetup() : begun_(native::begin_run()) {}
        ~RunSetup() { native::end_run(begun_); }
        RunSetup(const RunSetup&) = delete;
        RunSetup& operator=(const RunSetup&) = delete;
        RunSetup(RunSetup&&) = delete;
        RunSetup& operator=(RunSetup&&) = delete;

    private:
        bool begun_;
    };
    RunSetup run_setup_;

    std::uint32_t wake_event_ = 0;
    // Events taken from the OS directly (macOS trackpads), queued in order with SDL's: each one
    // pushes an SDL event of this type, which takes the next from native_events_.
    std::uint32_t native_event_ = 0;
    std::deque<Event> native_events_;
    void* input_monitor_ = nullptr;
    LiveResizeCallback live_resize_;
};

namespace {

bool g_platform_alive = false;

[[noreturn]] void throw_sdl_error(std::string_view what) {
    throw PlatformError(std::format("{}: {}", what, SDL_GetError()));
}

bool SDLCALL on_event_watch(void* userdata, SDL_Event* event) {
    // Events may be watched from whichever thread pushed them (e.g. wake() from the render
    // thread); live resize only happens on the main thread.
    if (!SDL_IsMainThread()) {
        return true;
    }
    // While the user drags a window edge, draw each new size as the OS applies it (the frame then
    // reaches the compositor with the size), and on SDL's live-resize timer for animations in
    // between. The timer alone is too irregular: WM_TIMER only comes when no other message waits.
    const bool live_expose = event->type == SDL_EVENT_WINDOW_EXPOSED && event->window.data1 == 1;
    const bool resized = event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && native::in_move_size_loop();
    if (live_expose || resized) {
        static_cast<SdlPlatform*>(userdata)->dispatch_live_resize(event->window.windowID);
    }
    return true;
}

Key translate_key(SDL_Keycode code) {
    if (code >= SDLK_A && code <= SDLK_Z) {
        return static_cast<Key>(static_cast<int>(Key::a) + static_cast<int>(code - SDLK_A));
    }
    if (code >= SDLK_0 && code <= SDLK_9) {
        return static_cast<Key>(static_cast<int>(Key::digit0) + static_cast<int>(code - SDLK_0));
    }
    if (code >= SDLK_F1 && code <= SDLK_F12) {
        return static_cast<Key>(static_cast<int>(Key::f1) + static_cast<int>(code - SDLK_F1));
    }
    switch (code) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return Key::enter;
    case SDLK_ESCAPE: return Key::escape;
    case SDLK_BACKSPACE: return Key::backspace;
    case SDLK_TAB: return Key::tab;
    case SDLK_SPACE: return Key::space;
    case SDLK_DELETE: return Key::forward_delete;
    case SDLK_INSERT: return Key::insert;
    case SDLK_HOME: return Key::home;
    case SDLK_END: return Key::end;
    case SDLK_PAGEUP: return Key::page_up;
    case SDLK_PAGEDOWN: return Key::page_down;
    case SDLK_LEFT: return Key::left;
    case SDLK_RIGHT: return Key::right;
    case SDLK_UP: return Key::up;
    case SDLK_DOWN: return Key::down;
    case SDLK_APPLICATION: return Key::menu;
    default: return Key::unknown;
    }
}

KeyModifiers translate_modifiers(SDL_Keymod mod) {
    KeyModifiers result = KeyModifiers::none;
    if (mod & SDL_KMOD_SHIFT) {
        result = result | KeyModifiers::shift;
    }
    if (mod & SDL_KMOD_CTRL) {
        result = result | KeyModifiers::control;
    }
    if (mod & SDL_KMOD_ALT) {
        result = result | KeyModifiers::alt;
    }
    if (mod & SDL_KMOD_GUI) {
        result = result | KeyModifiers::super;
    }
    return result;
}

// A mouse wheel notch, in points.
constexpr float kPointsPerNotch = 48;

Event translate(const SDL_Event& event, std::uint32_t wake_event) {
    if (event.type == wake_event) {
        return {EventType::wake};
    }
    switch (event.type) {
    case SDL_EVENT_QUIT: return {EventType::quit};
    case SDL_EVENT_WINDOW_EXPOSED:
        // Live-resize exposes (data1 == 1) were already handled by the event watch.
        return event.window.data1 == 0 ? Event{EventType::window_exposed, event.window.windowID} : Event{};
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        return {EventType::window_resized, event.window.windowID};
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        return {EventType::window_scale_changed, event.window.windowID};
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return {EventType::window_close_requested, event.window.windowID};
    case SDL_EVENT_WINDOW_MAXIMIZED: return {EventType::window_maximized, event.window.windowID};
    case SDL_EVENT_WINDOW_RESTORED: return {EventType::window_restored, event.window.windowID};
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_SHOWN: return {EventType::window_visibility_changed, event.window.windowID};
    case SDL_EVENT_WINDOW_FOCUS_GAINED: return {EventType::window_focus_gained, event.window.windowID};
    case SDL_EVENT_WINDOW_FOCUS_LOST: return {EventType::window_focus_lost, event.window.windowID};
    case SDL_EVENT_WINDOW_MOVED: return {EventType::window_moved, event.window.windowID};
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: return {EventType::window_fullscreen_changed, event.window.windowID};
    case SDL_EVENT_SYSTEM_THEME_CHANGED:
        return {EventType::system_theme_changed};
    case SDL_EVENT_DROP_BEGIN:
    case SDL_EVENT_DROP_POSITION:
    case SDL_EVENT_DROP_FILE:
    case SDL_EVENT_DROP_TEXT:
    case SDL_EVENT_DROP_COMPLETE: {
        const EventType type = event.type == SDL_EVENT_DROP_BEGIN      ? EventType::drop_begin
                               : event.type == SDL_EVENT_DROP_POSITION ? EventType::drop_position
                               : event.type == SDL_EVENT_DROP_FILE     ? EventType::drop_file
                               : event.type == SDL_EVENT_DROP_TEXT     ? EventType::drop_text
                                                                       : EventType::drop_complete;
        Event result{type, event.drop.windowID};
        result.x = event.drop.x;
        result.y = event.drop.y;
        result.text = event.drop.data ? event.drop.data : "";
        if (type == EventType::drop_text) { // not a file's path, which needn't be UTF-8
            make_valid_utf8(result.text);
        }
        return result;
    }
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        return {EventType::pointer_leave, event.window.windowID};
    case SDL_EVENT_WINDOW_HIT_TEST: {
        // Over a drag area (and the traffic lights above one) SDL sends this instead of the
        // pointer's motion, so what was hovered would stay hovered: move it here.
        SDL_Window* window = SDL_GetWindowFromID(event.window.windowID);
        int x = 0;
        int y = 0;
        if (!window || !SDL_GetWindowPosition(window, &x, &y)) {
            return {};
        }
        Event result{EventType::pointer_move, event.window.windowID};
        SDL_GetGlobalMouseState(&result.x, &result.y);
        result.x -= static_cast<float>(x);
        result.y -= static_cast<float>(y);
        return result;
    }
    case SDL_EVENT_MOUSE_MOTION: {
        Event result{EventType::pointer_move, event.motion.windowID};
        result.x = event.motion.x;
        result.y = event.motion.y;
        return result;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        Event result{event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EventType::pointer_down : EventType::pointer_up,
                     event.button.windowID};
        result.x = event.button.x;
        result.y = event.button.y;
        result.clicks = event.button.clicks;
        result.modifiers = translate_modifiers(SDL_GetModState());
        switch (event.button.button) {
        case SDL_BUTTON_LEFT: result.button = PointerButton::left; break;
        case SDL_BUTTON_MIDDLE: result.button = PointerButton::middle; break;
        case SDL_BUTTON_RIGHT: result.button = PointerButton::right; break;
        default: result.button = PointerButton::other; break;
        }
        return result;
    }
    case SDL_EVENT_TEXT_INPUT: {
        Event result{EventType::text_input, event.text.windowID};
        result.text = valid_utf8(event.text.text ? event.text.text : "");
        return result;
    }
    case SDL_EVENT_TEXT_EDITING: {
        Event result{EventType::text_editing, event.edit.windowID};
        result.text = valid_utf8(event.edit.text ? event.edit.text : "");
        // `start` counts characters; we want bytes.
        std::size_t byte = 0;
        for (Sint32 chars = std::max(event.edit.start, 0); chars > 0 && byte < result.text.size(); --chars) {
            do {
                ++byte;
            } while (byte < result.text.size() && (static_cast<unsigned char>(result.text[byte]) & 0xC0) == 0x80);
        }
        result.text_cursor = event.edit.start < 0 ? result.text.size() : byte;
        return result;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        // x/y already follow the user's "natural scrolling" setting (direction says if flipped).
        // SDL gives no other sign of a trackpad than fractional notches.
        Event result{EventType::wheel, event.wheel.windowID};
        result.x = event.wheel.mouse_x;
        result.y = event.wheel.mouse_y;
        result.scroll_dx = event.wheel.x * kPointsPerNotch;
        result.scroll_dy = -event.wheel.y * kPointsPerNotch;
        result.precise = event.wheel.x != std::trunc(event.wheel.x) || event.wheel.y != std::trunc(event.wheel.y);
        result.time = static_cast<double>(event.wheel.timestamp) * 1e-9;
        result.modifiers = translate_modifiers(SDL_GetModState());
        return result;
    }
    case SDL_EVENT_PINCH_BEGIN:
    case SDL_EVENT_PINCH_UPDATE:
    case SDL_EVENT_PINCH_END: {
        Event result{EventType::magnify, event.pinch.windowID};
        SDL_GetMouseState(&result.x, &result.y);
        result.amount = event.type == SDL_EVENT_PINCH_UPDATE ? event.pinch.scale - 1 : 0;
        result.phase = event.type == SDL_EVENT_PINCH_BEGIN    ? GesturePhase::began
                       : event.type == SDL_EVENT_PINCH_UPDATE ? GesturePhase::changed
                                                              : GesturePhase::ended;
        return result;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        Event result{event.type == SDL_EVENT_KEY_DOWN ? EventType::key_down : EventType::key_up,
                     event.key.windowID};
        result.key = translate_key(event.key.key);
        result.modifiers = translate_modifiers(event.key.mod);
        result.repeat = event.key.repeat;
        return result;
    }
    default: return {};
    }
}

} // namespace

// --- Window ---

namespace {

// A length or position in window coordinates, rounded to the nearest: NaN is 0, and values past
// int's range clamp to it (layout geometry can be either).
int window_pixels(float value) {
    return clamped_cast<int>(std::round(value));
}

// How many windows hold Secure Event Input (macOS): on while any does.
int secure_input_holders = 0;

void hold_secure_input(bool hold) {
    const int before = secure_input_holders;
    secure_input_holders += hold ? 1 : -1;
    if (before == 0 && secure_input_holders == 1) {
        native::set_secure_event_input(true);
    } else if (before == 1 && secure_input_holders == 0) {
        native::set_secure_event_input(false);
    }
}

} // namespace

SdlWindow::SdlWindow(const WindowDesc& desc, const PopupSpec* popup) {
    if (desc.width <= 0 || desc.height <= 0) {
        throw UsageError(std::format("window \"{}\" has an invalid size {}x{}", desc.title,
                                     desc.width, desc.height));
    }

    // WindowDesc sizes are logical points. SDL takes points on macOS (content scale 1) and
    // pixels on Windows, so scaling by the display's content scale gives the right size on both.
    float content_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (content_scale <= 0) {
        content_scale = 1;
    }

    // macOS presents through a CAMetalLayer; elsewhere WebGPU takes the native window as is.
    const auto kGpuFlags = static_cast<SDL_WindowFlags>(native::gpu_window_flags());
    SDL_WindowFlags flags = kGpuFlags | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    if (desc.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (desc.title_bar == TitleBar::hidden) {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    if (see_through(desc)) {
        flags |= SDL_WINDOW_TRANSPARENT;
    }
    if (desc.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN; // borderless at the desktop's mode (no mode was set)
    }

    const int width = window_pixels(static_cast<float>(desc.width) * content_scale);
    const int height = window_pixels(static_cast<float>(desc.height) * content_scale);
    if (popup) {
        // Menus and tooltips: always transparent and undecorated (they draw their own panel).
        flags = kGpuFlags | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN | SDL_WINDOW_TRANSPARENT |
                (popup->tooltip ? SDL_WINDOW_TOOLTIP : SDL_WINDOW_POPUP_MENU);
        parent_ = static_cast<const SdlWindow*>(popup->parent)->window_;
        window_ = SDL_CreatePopupWindow(parent_, 0, 0, std::max(1, width), std::max(1, height), flags);
    } else {
        window_ = SDL_CreateWindow(desc.title.c_str(), width, height, flags);
    }
    if (!window_) {
        throw_sdl_error("failed to create a window");
    }
    id_ = SDL_GetWindowID(window_);
    native_ = native::window_created(window_, desc, popup != nullptr);

    if (desc.min_size) {
        SDL_SetWindowMinimumSize(window_, window_pixels(desc.min_size->width * content_scale),
                                 window_pixels(desc.min_size->height * content_scale));
    }
    if (desc.opacity < 1) {
        SDL_SetWindowOpacity(window_, std::clamp(desc.opacity, 0.f, 1.f));
    }
}

void SdlWindow::set_popup_frame(Rect frame) {
    // Logical points -> window coordinates (the inverse of to_logical); popups are positioned
    // relative to their parent.
    float density = SDL_GetWindowPixelDensity(window_);
    if (density <= 0) {
        density = 1;
    }
    const float factor = scale() / density;
    // Rounded up: a window a fraction narrower than its content would cut it off (text ellipsized).
    SDL_SetWindowSize(window_, std::max(1, clamped_cast<int>(std::ceil(frame.width * factor - 0.01f))),
                      std::max(1, clamped_cast<int>(std::ceil(frame.height * factor - 0.01f))));
    SDL_SetWindowPosition(window_, window_pixels(frame.x * factor), window_pixels(frame.y * factor));
}

Rect SdlWindow::usable_area() const {
    SDL_Rect bounds{};
    if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window_), &bounds)) {
        return {-1e6f, -1e6f, 2e6f, 2e6f}; // unknown: don't constrain
    }
    int x = 0;
    int y = 0;
    SDL_GetWindowPosition(window_, &x, &y);
    const Point origin = to_logical(static_cast<float>(bounds.x - x), static_cast<float>(bounds.y - y));
    const Point size = to_logical(static_cast<float>(bounds.w), static_cast<float>(bounds.h));
    return {origin.x, origin.y, size.x, size.y};
}

bool draws_material(const WindowDesc& desc) {
    return desc.material != WindowMaterial::none && native::supports_material(desc.material);
}

bool see_through(const WindowDesc& desc) {
    return desc.transparent || draws_material(desc);
}

bool any_window_focused() {
    return SDL_GetKeyboardFocus() != nullptr;
}

std::uint32_t focused_window_id() {
    SDL_Window* window = SDL_GetKeyboardFocus();
    return window ? SDL_GetWindowID(window) : 0;
}

void SdlWindow::refresh_appearance() {
    native::appearance_changed(window_, native_);
}

void* SdlWindow::native_handle() const {
    return native::native_handle(window_);
}

void SdlWindow::minimize() {
    SDL_MinimizeWindow(window_);
}

void SdlWindow::toggle_maximized() {
    if (maximized()) {
        SDL_RestoreWindow(window_);
    } else {
        SDL_MaximizeWindow(window_);
    }
}

bool SdlWindow::maximized() const {
    return (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) != 0;
}

bool SdlWindow::visible() const {
    return (SDL_GetWindowFlags(window_) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) == 0;
}

Rect SdlWindow::system_controls_frame() {
    return native::system_controls_frame(native_);
}

void SdlWindow::request_close() const {
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    event.window.windowID = id_;
    SDL_PushEvent(&event);
}

namespace {

SDL_HitTestResult SDLCALL hit_test(SDL_Window*, const SDL_Point* area, void* data) {
    auto* window = static_cast<SdlWindow*>(data);
    try {
        switch (window->hit_test_at(window->to_logical(static_cast<float>(area->x), static_cast<float>(area->y)))) {
        case HitArea::normal: return SDL_HITTEST_NORMAL;
        case HitArea::drag: return SDL_HITTEST_DRAGGABLE;
        case HitArea::resize_top_left: return SDL_HITTEST_RESIZE_TOPLEFT;
        case HitArea::resize_top: return SDL_HITTEST_RESIZE_TOP;
        case HitArea::resize_top_right: return SDL_HITTEST_RESIZE_TOPRIGHT;
        case HitArea::resize_right: return SDL_HITTEST_RESIZE_RIGHT;
        case HitArea::resize_bottom_right: return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
        case HitArea::resize_bottom: return SDL_HITTEST_RESIZE_BOTTOM;
        case HitArea::resize_bottom_left: return SDL_HITTEST_RESIZE_BOTTOMLEFT;
        case HitArea::resize_left: return SDL_HITTEST_RESIZE_LEFT;
        }
    } catch (...) {
        // Swallowed: nothing may unwind through the window system; a failed test is an ordinary point.
    }
    return SDL_HITTEST_NORMAL;
}

} // namespace

void SdlWindow::set_hit_test(std::function<HitArea(Point)> test) {
    hit_test_ = std::move(test);
    SDL_SetWindowHitTest(window_, hit_test_ ? &hit_test : nullptr, this);
}

SdlWindow::~SdlWindow() {
    if (parent_text_input_) {
        SDL_StopTextInput(parent_); // closed with its text field focused
    }
    if (secure_engaged_) {
        hold_secure_input(false);
    }
    native::window_destroying(window_, native_);
    SDL_DestroyWindow(window_);
}

PixelSize SdlWindow::pixel_size() const {
    PixelSize size;
    SDL_GetWindowSizeInPixels(window_, &size.width, &size.height);
    return size;
}

float SdlWindow::scale() const {
    const float scale = SDL_GetWindowDisplayScale(window_);
    return scale > 0 ? scale : 1.f;
}

Size SdlWindow::logical_size() const {
    const PixelSize pixels = pixel_size();
    const float s = scale();
    return {static_cast<float>(pixels.width) / s, static_cast<float>(pixels.height) / s};
}

namespace {

void start_text_input_on(SDL_Window* window, bool secure) {
    if (!secure) {
        SDL_StartTextInput(window);
        return;
    }
    const SDL_PropertiesID properties = SDL_CreateProperties();
    SDL_SetNumberProperty(properties, SDL_PROP_TEXTINPUT_TYPE_NUMBER, SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN);
    SDL_SetBooleanProperty(properties, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
    SDL_StartTextInputWithProperties(window, properties);
    SDL_DestroyProperties(properties);
    native::detach_ime(window);
}

} // namespace

void SdlWindow::start_text_input(bool secure) {
    start_text_input_on(window_, secure);
    // A popup (a popover's text field): typing reaches the parent's native window, so SDL makes
    // text there. Unless the parent has text input of its own, it's started for the popup.
    if (parent_ && !parent_text_input_ && !SDL_TextInputActive(parent_)) {
        start_text_input_on(parent_, secure);
        parent_text_input_ = true;
    }
    secure_text_input_ = secure;
    update_secure_input();
}

void SdlWindow::stop_text_input() {
    SDL_StopTextInput(window_);
    if (std::exchange(parent_text_input_, false)) {
        SDL_StopTextInput(parent_);
    }
    secure_text_input_ = false;
    update_secure_input();
}

void SdlWindow::update_secure_input() {
    // A popup never has the keyboard; its parent does.
    SDL_Window* const keyboard = parent_ ? parent_ : window_;
    const bool engage = secure_text_input_ && (SDL_GetWindowFlags(keyboard) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (engage != secure_engaged_) {
        secure_engaged_ = engage;
        hold_secure_input(engage);
    }
}

void SdlWindow::set_text_input_area(Rect area, float cursor) {
    // Logical points -> window coordinates (the inverse of to_logical).
    float density = SDL_GetWindowPixelDensity(window_);
    if (density <= 0) {
        density = 1;
    }
    const float factor = scale() / density;
    const SDL_Rect rect{window_pixels(area.x * factor), window_pixels(area.y * factor),
                        window_pixels(area.width * factor), window_pixels(area.height * factor)};
    SDL_SetTextInputArea(window_, &rect, window_pixels(cursor * factor));
}

std::string clipboard_text() {
    char* text = SDL_GetClipboardText();
    std::string result = valid_utf8(text ? text : "");
    SDL_free(text);
    return result;
}

void set_clipboard_text(const std::string& text) {
    SDL_SetClipboardText(text.c_str());
}

void set_pointer_style(PointerStyle style) {
    // Created on first use; SDL frees them at shutdown.
    const auto system = [](SDL_SystemCursor id) {
        static SDL_Cursor* cursors[SDL_SYSTEM_CURSOR_COUNT] = {};
        if (!cursors[id]) {
            cursors[id] = SDL_CreateSystemCursor(id);
        }
        return cursors[id];
    };
    SDL_SystemCursor id = SDL_SYSTEM_CURSOR_DEFAULT;
    switch (style) {
    case PointerStyle::automatic:
    case PointerStyle::arrow: id = SDL_SYSTEM_CURSOR_DEFAULT; break;
    case PointerStyle::pointer: id = SDL_SYSTEM_CURSOR_POINTER; break;
    case PointerStyle::text: id = SDL_SYSTEM_CURSOR_TEXT; break;
    case PointerStyle::crosshair: id = SDL_SYSTEM_CURSOR_CROSSHAIR; break;
    case PointerStyle::move: id = SDL_SYSTEM_CURSOR_MOVE; break;
    case PointerStyle::wait: id = SDL_SYSTEM_CURSOR_WAIT; break;
    case PointerStyle::progress: id = SDL_SYSTEM_CURSOR_PROGRESS; break;
    case PointerStyle::resize_horizontal: id = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
    case PointerStyle::resize_vertical: id = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
    case PointerStyle::resize_nwse: id = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
    case PointerStyle::resize_nesw: id = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
    case PointerStyle::not_allowed: id = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
    }
    SDL_SetCursor(system(id));
}

Point SdlWindow::to_logical(float x, float y) const {
    // Window coordinates -> pixels (pixel density, e.g. 2 on Retina) -> points (display scale).
    float density = SDL_GetWindowPixelDensity(window_);
    if (density <= 0) {
        density = 1;
    }
    const float factor = density / scale();
    return {x * factor, y * factor};
}

void SdlWindow::show() {
    SDL_ShowWindow(window_);
}

bool Window::shows_before_first_frame() {
    return std::string_view(SDL_GetCurrentVideoDriver()) == "wayland";
}

SurfaceSource SdlWindow::surface_source() const {
    const SurfaceSource source = native::surface_source(window_, native_);
    if (!source.handle && source.xlib_window == 0) {
        throw GpuError(std::format("the window system gave no handle to draw into: {}", SDL_GetError()));
    }
    return source;
}

// --- Platform ---

SdlPlatform::SdlPlatform() {
    if (g_platform_alive) {
        throw UsageError("only one elg::App can run at a time");
    }
    // We draw the IME's composition inline (TextField); the system shows its candidate list.
    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "composition");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw_sdl_error("failed to initialize SDL");
    }
    g_platform_alive = true;

    wake_event_ = SDL_RegisterEvents(2);
    native_event_ = wake_event_ + 1;
    SDL_AddEventWatch(&on_event_watch, this);
    input_monitor_ = native::install_input_monitor([this](Event event) {
        event.modifiers = translate_modifiers(SDL_GetModState());
        if (event.type == EventType::wheel && !event.precise) {
            event.scroll_dx *= kPointsPerNotch;
            event.scroll_dy *= kPointsPerNotch;
        }
        native_events_.push_back(std::move(event));
        SDL_Event marker{};
        marker.type = native_event_;
        SDL_PushEvent(&marker);
        return true;
    });
    native::watch_appearance();
}

SdlPlatform::~SdlPlatform() {
    native::unwatch_appearance();
    native::remove_input_monitor(input_monitor_);
    SDL_RemoveEventWatch(&on_event_watch, this);
    SDL_Quit();
    g_platform_alive = false;
}

Event SdlPlatform::wait_event() {
    SDL_Event event;
    if (!SDL_WaitEvent(&event)) {
        throw_sdl_error("failed to wait for events");
    }
    return translate(event);
}

Event SdlPlatform::wait_event(double seconds) {
    SDL_Event event;
    const auto milliseconds = static_cast<Sint32>(std::clamp(std::ceil(seconds * 1000), 0.0, 1e6));
    if (!SDL_WaitEventTimeout(&event, milliseconds)) {
        return {}; // timed out
    }
    return translate(event);
}

std::optional<Event> SdlPlatform::poll_event() {
    SDL_Event event;
    if (!SDL_PollEvent(&event)) {
        return std::nullopt;
    }
    return translate(event);
}

Event SdlPlatform::translate(const SDL_Event& event) {
    if (event.type == native_event_) {
        if (native_events_.empty()) {
            return {};
        }
        Event result = std::move(native_events_.front());
        native_events_.pop_front();
        return result;
    }
    return platform::translate(event, wake_event_);
}

void SdlPlatform::post_key(std::uint32_t window_id, Key key, KeyModifiers modifiers) {
    for (const EventType type : {EventType::key_down, EventType::key_up}) {
        Event event{type, window_id};
        event.key = key;
        event.modifiers = modifiers;
        event.synthetic = true;
        native_events_.push_back(std::move(event));
        SDL_Event marker{};
        marker.type = native_event_;
        SDL_PushEvent(&marker);
    }
}

void SdlPlatform::wake() const {
    SDL_Event event{};
    event.type = wake_event_;
    SDL_PushEvent(&event);
}

void SdlPlatform::set_live_resize_callback(LiveResizeCallback callback) {
    live_resize_ = std::move(callback);
}

void SdlPlatform::dispatch_live_resize(std::uint32_t window_id) noexcept {
    if (!live_resize_) {
        return;
    }
    // We're inside an SDL callback (a C frame): nothing may unwind through it.
    try {
        live_resize_(window_id);
    } catch (const std::exception& e) {
        internal_error(std::format("exception escaped the live-resize handler: {}", e.what()));
    } catch (...) {
        internal_error("unknown exception escaped the live-resize handler");
    }
}

// --- Free functions ---

bool system_prefers_dark() {
    return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK;
}

// --- Factories ---

std::unique_ptr<Window> create_window(const WindowDesc& desc, const PopupSpec* popup) {
    return std::make_unique<SdlWindow>(desc, popup);
}

std::unique_ptr<Platform> create_platform() {
    return std::make_unique<SdlPlatform>();
}

} // namespace elg::detail::platform
