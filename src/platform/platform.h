// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_PLATFORM_H
#define ELG_PLATFORM_PLATFORM_H

// Internal platform layer (design.md §4). SDL3 is the only backend, but no SDL types leak out
// of platform/*.cpp, so it could be replaced without touching the rest of the framework.

#include <elg/app.h>
#include <elg/events.h>
#include <elg/geometry.h>

#include <volk.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;

namespace elg::detail::platform {

struct PixelSize {
    int width = 0;
    int height = 0;
};

enum class EventType {
    none,
    quit,
    wake,
    window_exposed,
    window_resized,
    window_scale_changed,
    window_close_requested,
    pointer_move,
    pointer_down,
    pointer_up,
    pointer_leave,
    wheel,
    text_input,
    text_editing,
    key_down,
    key_up,
    window_maximized,
    window_restored,
    window_focus_gained,
    window_focus_lost,
    window_moved,
    system_theme_changed,
    // Drag and drop from other apps: begin, position (while over the window), a file or text
    // (one event each), complete (dropped, or the drag left).
    drop_begin,
    drop_position,
    drop_file,
    drop_text,
    drop_complete,
};

enum class PointerButton { left, middle, right, other };

struct Event {
    EventType type = EventType::none;
    std::uint32_t window_id = 0;
    // Pointer events: position in window coordinates (convert with Window::to_logical).
    float x = 0;
    float y = 0;
    PointerButton button = PointerButton::left;
    int clicks = 1;
    // Wheel events: notches (fractional for trackpads), positive right / away from the user.
    float wheel_x = 0;
    float wheel_y = 0;
    bool precise = false;
    // Text events: the text, and for editing (IME composition) the caret in bytes.
    std::string text;
    std::size_t text_cursor = 0;
    // Key events (modifiers also for pointer buttons).
    Key key = Key::unknown;
    KeyModifiers modifiers = KeyModifiers::none;
    bool repeat = false;
};

// What part of a window a point is, for windows without system decorations.
enum class HitArea {
    normal,
    drag, // moves the window (a custom title bar)
    resize_top_left,
    resize_top,
    resize_top_right,
    resize_right,
    resize_bottom_right,
    resize_bottom,
    resize_bottom_left,
    resize_left,
};

class Window;

// A popup (menu or tooltip) window attached to `parent` (design.md §13.1).
struct PopupSpec {
    const Window* parent = nullptr;
    bool tooltip = false;
};

class Window {
public:
    // Created hidden; call show() once the first frame is queued. With `popup`, the window is a
    // borderless, transparent popup of its parent (place it with set_popup_frame()).
    explicit Window(const WindowDesc& desc, const PopupSpec* popup = nullptr);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    std::uint32_t id() const noexcept { return id_; }
    PixelSize pixel_size() const;
    // Pixels per logical point.
    float scale() const;
    Size logical_size() const;
    // Converts window coordinates (as in pointer events) to logical points.
    Point to_logical(float x, float y) const;
    // Text input (IME) for this window, and where the text being edited is (logical points).
    void start_text_input();
    void stop_text_input();
    void set_text_input_area(Rect area, float cursor);

    void show();
    // The window system's handle (HWND on Windows), or nullptr.
    void* native_handle() const;

    void minimize();
    void toggle_maximized();
    bool maximized() const;
    // Asks to close, like the system close button (arrives as window_close_requested).
    void request_close();
    // Decides drag and resize areas (logical points) for windows without system decorations.
    // Called from inside event processing, on the UI thread.
    void set_hit_test(std::function<HitArea(Point)> test);

    // Popups: position and size, in the parent's logical points relative to its content.
    void set_popup_frame(Rect frame);
    // The usable area of this window's display (without taskbars), in this window's logical
    // points relative to its content.
    Rect usable_area() const;
    HitArea hit_test_at(Point point) const { return hit_test_ ? hit_test_(point) : HitArea::normal; }

    // The caller owns the surface and must destroy it before this window.
    [[nodiscard]] VkSurfaceKHR create_surface(VkInstance instance) const;

private:
    SDL_Window* window_ = nullptr;
    std::uint32_t id_ = 0;
    std::function<HitArea(Point)> hit_test_;
};

// Owns SDL's lifetime. Construct on the main thread, which becomes the UI thread.
// Whether one of the app's windows has keyboard focus.
bool any_window_focused();
// Blocks until the system compositor has composed its next frame (DwmFlush on Windows; nothing
// elsewhere). During a live resize this lets a just-presented frame reach the screen before the
// window is resized again, so the compositor doesn't stretch an old frame.
void wait_for_compositor();
std::string clipboard_text();
void set_clipboard_text(const std::string& text);
// The pointer's shape: the arrow, or the I-beam over text.
void set_text_cursor(bool text);

class Platform {
public:
    using LiveResizeCallback = std::move_only_function<void(std::uint32_t window_id)>;

    Platform();
    ~Platform();

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;

    std::vector<const char*> vulkan_instance_extensions() const;

    // Blocks until an event arrives.
    Event wait_event();
    // Blocks until an event arrives or `seconds` pass (then returns EventType::none).
    Event wait_event(double seconds);
    std::optional<Event> poll_event();

    // Wakes wait_event() with EventType::wake. Thread-safe.
    void wake();

    // Called on the main thread from inside OS modal loops while the user resizes a window,
    // when the normal event loop is blocked. Must not throw.
    void set_live_resize_callback(LiveResizeCallback callback);
    void dispatch_live_resize(std::uint32_t window_id) noexcept;

private:
    std::uint32_t wake_event_ = 0;
    LiveResizeCallback live_resize_;
};

// Per-user writable directory for Elegence data (crash reports, caches).
std::filesystem::path pref_path();
std::optional<std::string> environment_variable(const char* name);
// Whether the OS is set to a dark appearance.
bool system_prefers_dark();
bool debugger_present() noexcept;
void debug_break() noexcept;
void show_error_box(const char* title, const char* message) noexcept;

} // namespace elg::detail::platform

#endif // ELG_PLATFORM_PLATFORM_H
