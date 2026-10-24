// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "platform/platform.h"

#include "core/crash.h"
#include "core/log.h"

#include <elg/error.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#include <dwmapi.h>
#elif defined(__linux__)
#include <csignal>
#include <fstream>
#else
#include <csignal>
#endif

namespace elg::detail::platform {

namespace {

bool g_platform_alive = false;

[[noreturn]] void throw_sdl_error(std::string_view what) {
    throw PlatformError(std::format("{}: {}", what, SDL_GetError()));
}

// Whether the OS is running its modal move/size loop on this thread (the user is dragging a
// window edge), during which our event loop doesn't run.
bool in_move_size_loop() {
#if defined(_WIN32)
    GUITHREADINFO info{sizeof(GUITHREADINFO)};
    return GetGUIThreadInfo(GetCurrentThreadId(), &info) && (info.flags & GUI_INMOVESIZE) != 0;
#else
    return false;
#endif
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
    const bool resized = event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED && in_move_size_loop();
    if (live_expose || resized) {
        static_cast<Platform*>(userdata)->dispatch_live_resize(event->window.windowID);
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

Event translate(const SDL_Event& event, std::uint32_t wake_event) {
    if (event.type == wake_event) {
        return {EventType::wake};
    }
    switch (event.type) {
    case SDL_EVENT_QUIT: return {EventType::quit};
    case SDL_EVENT_WINDOW_EXPOSED:
        // Live-resize exposes were already handled by the event watch.
        return event.window.data1 == 1 ? Event{} : Event{EventType::window_exposed, event.window.windowID};
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        return {EventType::window_resized, event.window.windowID};
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        return {EventType::window_scale_changed, event.window.windowID};
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return {EventType::window_close_requested, event.window.windowID};
    case SDL_EVENT_WINDOW_MAXIMIZED: return {EventType::window_maximized, event.window.windowID};
    case SDL_EVENT_WINDOW_RESTORED: return {EventType::window_restored, event.window.windowID};
    case SDL_EVENT_WINDOW_FOCUS_GAINED: return {EventType::window_focus_gained, event.window.windowID};
    case SDL_EVENT_WINDOW_FOCUS_LOST: return {EventType::window_focus_lost, event.window.windowID};
    case SDL_EVENT_WINDOW_MOVED: return {EventType::window_moved, event.window.windowID};
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
        return result;
    }
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        return {EventType::pointer_leave, event.window.windowID};
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
        result.text = event.text.text ? event.text.text : "";
        return result;
    }
    case SDL_EVENT_TEXT_EDITING: {
        Event result{EventType::text_editing, event.edit.windowID};
        result.text = event.edit.text ? event.edit.text : "";
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
        Event result{EventType::wheel, event.wheel.windowID};
        result.x = event.wheel.mouse_x;
        result.y = event.wheel.mouse_y;
        result.wheel_x = event.wheel.x;
        result.wheel_y = event.wheel.y;
        result.precise = event.wheel.x != std::trunc(event.wheel.x) || event.wheel.y != std::trunc(event.wheel.y);
        result.modifiers = translate_modifiers(SDL_GetModState());
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

Window::Window(const WindowDesc& desc, const PopupSpec* popup) {
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

    SDL_WindowFlags flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN;
    if (desc.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (desc.title_bar == TitleBar::hidden) {
        flags |= SDL_WINDOW_BORDERLESS;
    }
    if (desc.transparent) {
        flags |= SDL_WINDOW_TRANSPARENT;
    }
    if (desc.fullscreen) {
        flags |= SDL_WINDOW_FULLSCREEN; // borderless at the desktop's mode (no mode was set)
    }

    if (popup) {
        // Menus and tooltips: always transparent and undecorated (they draw their own panel).
        flags = SDL_WINDOW_VULKAN | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN | SDL_WINDOW_TRANSPARENT |
                (popup->tooltip ? SDL_WINDOW_TOOLTIP : SDL_WINDOW_POPUP_MENU);
        parent_ = popup->parent->window_;
        window_ = SDL_CreatePopupWindow(parent_, 0, 0,
                                        std::max(1, static_cast<int>(std::lround(desc.width * content_scale))),
                                        std::max(1, static_cast<int>(std::lround(desc.height * content_scale))), flags);
    } else {
        window_ = SDL_CreateWindow(desc.title.c_str(),
                                   static_cast<int>(std::lround(desc.width * content_scale)),
                                   static_cast<int>(std::lround(desc.height * content_scale)), flags);
    }
    if (!window_) {
        throw_sdl_error("failed to create a window");
    }
    id_ = SDL_GetWindowID(window_);

    if (desc.min_size) {
        SDL_SetWindowMinimumSize(window_, static_cast<int>(std::lround(desc.min_size->width * content_scale)),
                                 static_cast<int>(std::lround(desc.min_size->height * content_scale)));
    }
    if (desc.opacity < 1) {
        SDL_SetWindowOpacity(window_, std::clamp(desc.opacity, 0.f, 1.f));
    }
#if defined(_WIN32)
    // Windows 11 rounds the corners of decorated windows; ask the same for undecorated opaque
    // ones (transparent windows draw their own shape). Ignored on Windows 10.
    if (desc.title_bar == TitleBar::hidden && !desc.transparent) {
        if (auto* hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window_),
                                                                   SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr))) {
            const DWORD round = 2; // DWMWCP_ROUND
            DwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &round, sizeof(round));
        }
    }
#endif
}

void Window::set_popup_frame(Rect frame) {
    // Logical points -> window coordinates (the inverse of to_logical); popups are positioned
    // relative to their parent.
    float density = SDL_GetWindowPixelDensity(window_);
    if (density <= 0) {
        density = 1;
    }
    const float factor = scale() / density;
    SDL_SetWindowSize(window_, std::max(1, static_cast<int>(std::lround(frame.width * factor))),
                      std::max(1, static_cast<int>(std::lround(frame.height * factor))));
    SDL_SetWindowPosition(window_, static_cast<int>(std::lround(frame.x * factor)),
                          static_cast<int>(std::lround(frame.y * factor)));
}

Rect Window::usable_area() const {
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

bool any_window_focused() {
    return SDL_GetKeyboardFocus() != nullptr;
}

void wait_for_compositor() {
#if defined(_WIN32)
    DwmFlush();
#endif
}

void* Window::native_handle() const {
#if defined(_WIN32)
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    return nullptr;
#endif
}

void Window::minimize() {
    SDL_MinimizeWindow(window_);
}

void Window::toggle_maximized() {
    if (maximized()) {
        SDL_RestoreWindow(window_);
    } else {
        SDL_MaximizeWindow(window_);
    }
}

bool Window::maximized() const {
    return (SDL_GetWindowFlags(window_) & SDL_WINDOW_MAXIMIZED) != 0;
}

void Window::request_close() {
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_CLOSE_REQUESTED;
    event.window.windowID = id_;
    SDL_PushEvent(&event);
}

namespace {

SDL_HitTestResult SDLCALL hit_test(SDL_Window*, const SDL_Point* area, void* data) {
    auto* window = static_cast<Window*>(data);
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
        // Nothing may unwind through the window system; a failed test is an ordinary point.
    }
    return SDL_HITTEST_NORMAL;
}

} // namespace

void Window::set_hit_test(std::function<HitArea(Point)> test) {
    hit_test_ = std::move(test);
    SDL_SetWindowHitTest(window_, hit_test_ ? &hit_test : nullptr, this);
}

Window::~Window() {
    if (parent_text_input_) {
        SDL_StopTextInput(parent_); // closed with its text field focused
    }
    SDL_DestroyWindow(window_);
}

PixelSize Window::pixel_size() const {
    PixelSize size;
    SDL_GetWindowSizeInPixels(window_, &size.width, &size.height);
    return size;
}

float Window::scale() const {
    const float scale = SDL_GetWindowDisplayScale(window_);
    return scale > 0 ? scale : 1.f;
}

Size Window::logical_size() const {
    const PixelSize pixels = pixel_size();
    const float s = scale();
    return {static_cast<float>(pixels.width) / s, static_cast<float>(pixels.height) / s};
}

void Window::start_text_input() {
    SDL_StartTextInput(window_);
    // A popup (a popover's text field): typing reaches the parent's native window, so SDL makes
    // text there. Unless the parent has text input of its own, it's started for the popup.
    if (parent_ && !parent_text_input_ && !SDL_TextInputActive(parent_)) {
        SDL_StartTextInput(parent_);
        parent_text_input_ = true;
    }
}

void Window::stop_text_input() {
    SDL_StopTextInput(window_);
    if (std::exchange(parent_text_input_, false)) {
        SDL_StopTextInput(parent_);
    }
}

void Window::set_text_input_area(Rect area, float cursor) {
    // Logical points -> window coordinates (the inverse of to_logical).
    float density = SDL_GetWindowPixelDensity(window_);
    if (density <= 0) {
        density = 1;
    }
    const float factor = scale() / density;
    const SDL_Rect rect{static_cast<int>(std::lround(area.x * factor)), static_cast<int>(std::lround(area.y * factor)),
                        static_cast<int>(std::lround(area.width * factor)),
                        static_cast<int>(std::lround(area.height * factor))};
    SDL_SetTextInputArea(window_, &rect, static_cast<int>(std::lround(cursor * factor)));
}

std::string clipboard_text() {
    char* text = SDL_GetClipboardText();
    std::string result = text ? text : "";
    SDL_free(text);
    return result;
}

void set_clipboard_text(const std::string& text) {
    SDL_SetClipboardText(text.c_str());
}

void set_cursor(SystemCursor cursor) {
    // Created on first use; SDL frees them at shutdown.
    const auto system = [](SDL_SystemCursor id) {
        static SDL_Cursor* cursors[SDL_SYSTEM_CURSOR_COUNT] = {};
        if (!cursors[id]) {
            cursors[id] = SDL_CreateSystemCursor(id);
        }
        return cursors[id];
    };
    SDL_SystemCursor id = SDL_SYSTEM_CURSOR_DEFAULT;
    switch (cursor) {
    case SystemCursor::arrow: id = SDL_SYSTEM_CURSOR_DEFAULT; break;
    case SystemCursor::text: id = SDL_SYSTEM_CURSOR_TEXT; break;
    case SystemCursor::resize_horizontal: id = SDL_SYSTEM_CURSOR_EW_RESIZE; break;
    case SystemCursor::resize_vertical: id = SDL_SYSTEM_CURSOR_NS_RESIZE; break;
    case SystemCursor::pointer: id = SDL_SYSTEM_CURSOR_POINTER; break;
    case SystemCursor::crosshair: id = SDL_SYSTEM_CURSOR_CROSSHAIR; break;
    case SystemCursor::move: id = SDL_SYSTEM_CURSOR_MOVE; break;
    case SystemCursor::resize_nwse: id = SDL_SYSTEM_CURSOR_NWSE_RESIZE; break;
    case SystemCursor::resize_nesw: id = SDL_SYSTEM_CURSOR_NESW_RESIZE; break;
    case SystemCursor::not_allowed: id = SDL_SYSTEM_CURSOR_NOT_ALLOWED; break;
    }
    SDL_SetCursor(system(id));
}

Point Window::to_logical(float x, float y) const {
    // Window coordinates -> pixels (pixel density, e.g. 2 on Retina) -> points (display scale).
    float density = SDL_GetWindowPixelDensity(window_);
    if (density <= 0) {
        density = 1;
    }
    const float factor = density / scale();
    return {x * factor, y * factor};
}

void Window::show() {
    SDL_ShowWindow(window_);
}

VkSurfaceKHR Window::create_surface(VkInstance instance) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(window_, instance, nullptr, &surface)) {
        throw GpuError(std::format("failed to create a Vulkan surface: {}", SDL_GetError()));
    }
    return surface;
}

// --- Platform ---

Platform::Platform() {
    if (g_platform_alive) {
        throw UsageError("only one elg::App can run at a time");
    }
    // We draw the IME's composition inline (TextField); the system shows its candidate list.
    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "composition");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw_sdl_error("failed to initialize SDL");
    }
    g_platform_alive = true;

    wake_event_ = SDL_RegisterEvents(1);
    SDL_AddEventWatch(&on_event_watch, this);
}

Platform::~Platform() {
    SDL_RemoveEventWatch(&on_event_watch, this);
    SDL_Quit();
    g_platform_alive = false;
}

std::vector<const char*> Platform::vulkan_instance_extensions() const {
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (!names) {
        throw GpuError(std::format("Vulkan is not available: {}", SDL_GetError()));
    }
    return {names, names + count};
}

Event Platform::wait_event() {
    SDL_Event event;
    if (!SDL_WaitEvent(&event)) {
        throw_sdl_error("failed to wait for events");
    }
    return translate(event, wake_event_);
}

Event Platform::wait_event(double seconds) {
    SDL_Event event;
    const auto milliseconds = static_cast<Sint32>(std::clamp(std::ceil(seconds * 1000), 0.0, 1e6));
    if (!SDL_WaitEventTimeout(&event, milliseconds)) {
        return {}; // timed out
    }
    return translate(event, wake_event_);
}

std::optional<Event> Platform::poll_event() {
    SDL_Event event;
    if (!SDL_PollEvent(&event)) {
        return std::nullopt;
    }
    return translate(event, wake_event_);
}

void Platform::wake() {
    SDL_Event event{};
    event.type = wake_event_;
    SDL_PushEvent(&event);
}

void Platform::set_live_resize_callback(LiveResizeCallback callback) {
    live_resize_ = std::move(callback);
}

void Platform::dispatch_live_resize(std::uint32_t window_id) noexcept {
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

std::filesystem::path pref_path() {
    char* path = SDL_GetPrefPath("Elegence", "Elegence");
    if (!path) {
        return std::filesystem::temp_directory_path() / "elegence";
    }
    std::filesystem::path result(std::u8string_view(reinterpret_cast<const char8_t*>(path)));
    SDL_free(path);
    return result;
}

std::optional<std::string> environment_variable(const char* name) {
    if (const char* value = SDL_getenv(name)) {
        return std::string(value);
    }
    return std::nullopt;
}

bool system_prefers_dark() {
    return SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK;
}

bool debugger_present() noexcept {
#if defined(_WIN32)
    return IsDebuggerPresent() != 0;
#elif defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.starts_with("TracerPid:")) {
            return line.find_first_not_of(" \t0", 10) != std::string::npos;
        }
    }
    return false;
#else
    return false; // TODO: macOS (sysctl P_TRACED)
#endif
}

void debug_break() noexcept {
#if defined(_WIN32)
    __debugbreak();
#else
    std::raise(SIGTRAP);
#endif
}

void show_error_box(const char* title, const char* message) noexcept {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, nullptr);
}

} // namespace elg::detail::platform
