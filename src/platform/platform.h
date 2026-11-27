// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_PLATFORM_H
#define ELG_PLATFORM_PLATFORM_H

// The platform interface (design.md §4, ELGSPEC-0001.R1): what the rest of the library uses for
// windows, input and the system. SDL3 is the only backend, and no SDL types leak out of
// platform/, so it could be replaced without touching the rest of the framework. The headers
// here are the interface; sdl/ implements it the same on every OS, and windows/, macos/ and
// linux/ hold what each OS does differently.

#include <elg/app.h>
#include <elg/events.h>
#include <elg/geometry.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace elg::detail::platform {

struct PixelSize {
    int width = 0;
    int height = 0;
};

// What the GPU layer needs to create a surface for a window: the window system's own handles.
struct SurfaceSource {
    enum class Kind { none, win32, metal, xlib, wayland };
    Kind kind = Kind::none;
    void* display = nullptr; // HINSTANCE | Display* | wl_display*
    void* handle = nullptr;  // HWND | CAMetalLayer* | wl_surface*
    std::uint64_t xlib_window = 0;
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
    window_visibility_changed, // shown, hidden or minimized (Window::visible())
    window_focus_gained,
    window_focus_lost,
    window_moved,
    window_fullscreen_changed,
    magnify,       // trackpad pinch
    rotate,        // two fingers turning
    two_finger_double_tap,
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
    // Wheel events: points, positive toward the end of the content (right, down); precise for
    // trackpads, whose deltas apply as they come, rather than a wheel's notches.
    float scroll_dx = 0;
    float scroll_dy = 0;
    bool precise = false;
    // Wheel, magnify and rotate events: the trackpad gesture's phase (scrolls: then momentum).
    GesturePhase phase = GesturePhase::none;
    bool momentum = false;
    // Magnify: the change in scale (0.1 = 10% larger). Rotate: degrees, clockwise.
    float amount = 0;
    // Wheel events: when they happened (seconds; the clock depends on the source), or 0.
    double time = 0;
    // Text events: the text, and for editing (IME composition) the caret in bytes.
    std::string text{};
    std::size_t text_cursor = 0;
    // Key events (modifiers also for pointer buttons and the wheel).
    Key key = Key::unknown;
    KeyModifiers modifiers = KeyModifiers::none;
    bool repeat = false;
    // Made by the app (Platform::post_key), not typed.
    bool synthetic = false;
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

// A window of the app, which the platform implementation makes (create_window()).
class Window {
public:
    virtual ~Window() = default;

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;

    [[nodiscard]] virtual std::uint32_t id() const noexcept = 0;
    [[nodiscard]] virtual PixelSize pixel_size() const = 0;
    // Pixels per logical point.
    [[nodiscard]] virtual float scale() const = 0;
    [[nodiscard]] virtual Size logical_size() const = 0;
    // Converts window coordinates (as in pointer events) to logical points.
    [[nodiscard]] virtual Point to_logical(float x, float y) const = 0;
    // Text input (IME) for this window, and where the text being edited is (logical points).
    // `secure`: a password's, as the platforms' password fields do it: SDL's password input type
    // (Wayland turns the IME off), the IME detached (Windows), and Secure Event Input while the
    // window has the keyboard (macOS: no IME, and other apps can't read the keys).
    virtual void start_text_input(bool secure) = 0;
    virtual void stop_text_input() = 0;
    // Call when any window gained or lost keyboard focus: macOS Secure Event Input is on only
    // while the window with the password field has it (it's system-wide).
    virtual void update_secure_input() = 0;
    virtual void set_text_input_area(Rect area, float cursor) = 0;

    virtual void show() = 0;
    // Whether to show the window before drawing its first frame, rather than after. Wayland
    // forbids drawing into a window before the compositor configures it, which showing does
    // (and maps windows only once they have a frame, so they never appear blank there anyway).
    [[nodiscard]] static bool shows_before_first_frame();
    // The window system's handle (HWND on Windows), or nullptr.
    [[nodiscard]] virtual void* native_handle() const = 0;

    virtual void minimize() = 0;
    virtual void toggle_maximized() = 0;
    [[nodiscard]] virtual bool maximized() const = 0;
    // Neither hidden nor minimized.
    [[nodiscard]] virtual bool visible() const = 0;
    // Asks to close, like the system close button (arrives as window_close_requested).
    virtual void request_close() const = 0;
    // Where the system draws its window controls over the content (TitleBar::integrated on
    // macOS), in logical points; empty elsewhere and in fullscreen. Integrates the title bar again
    // first, as leaving fullscreen undoes it: call after window_fullscreen_changed.
    virtual Rect system_controls_frame() = 0;
    // Decides drag and resize areas (logical points) for windows without system decorations.
    // Called from inside event processing, on the UI thread.
    virtual void set_hit_test(std::function<HitArea(Point)> test) = 0;

    // Popups: position and size, in the parent's logical points relative to its content.
    virtual void set_popup_frame(Rect frame) = 0;
    // The usable area of this window's display (without taskbars), in this window's logical
    // points relative to its content.
    [[nodiscard]] virtual Rect usable_area() const = 0;
    [[nodiscard]] virtual HitArea hit_test_at(Point point) const = 0;

    // The system's light/dark setting changed: what the OS draws itself follows it (Windows'
    // title bar and Mica).
    virtual void refresh_appearance() = 0;

    // The handles the render thread creates this window's surface from. The surface must be
    // gone before this window.
    [[nodiscard]] virtual SurfaceSource surface_source() const = 0;

protected:
    Window() = default;
};

// Makes a window, hidden: call show() once its first frame is queued. With `popup`, the window
// is a borderless, transparent popup of its parent (place it with set_popup_frame()). While a
// Platform exists.
std::unique_ptr<Window> create_window(const WindowDesc& desc, const PopupSpec* popup = nullptr);

// Whether the system draws the window's material (WindowDesc::material) behind it here.
bool draws_material(const WindowDesc& desc);
// Whether the desktop can show through the window: transparent, or with a material drawn. Its
// default background is then clear and its surface composited with alpha.
bool see_through(const WindowDesc& desc);

// Whether one of the app's windows has keyboard focus.
bool any_window_focused();
// The window with keyboard focus (0: none).
std::uint32_t focused_window_id();
// Blocks until the system compositor has composed its next frame (DwmFlush on Windows; nothing
// elsewhere). During a live resize this lets a just-presented frame reach the screen before the
// window is resized again, so the compositor doesn't stretch an old frame.
void wait_for_compositor();
// Whether the app asks for the window's kept image again (WindowFrame::show_again) in the frame that follows
// EventType::window_exposed (ELGSPEC-0040.R25): true on Mikros, where SDL applies a new size only after the window
// commits a frame; false elsewhere.
bool presents_unchanged_exposes();
std::string clipboard_text();
void set_clipboard_text(const std::string& text);
// The pointer's shape (automatic: the arrow).
void set_pointer_style(PointerStyle style);

// The window system's event loop: one at a time, made by create_platform().
class Platform {
public:
    using LiveResizeCallback = MoveOnlyFunction<void(std::uint32_t window_id)>;

    virtual ~Platform() = default;

    Platform(const Platform&) = delete;
    Platform& operator=(const Platform&) = delete;
    Platform(Platform&&) = delete;
    Platform& operator=(Platform&&) = delete;

    // Blocks until an event arrives.
    virtual Event wait_event() = 0;
    // Blocks until an event arrives or `seconds` pass (then returns EventType::none).
    virtual Event wait_event(double seconds) = 0;
    virtual std::optional<Event> poll_event() = 0;

    // Wakes wait_event() with EventType::wake. Thread-safe.
    virtual void wake() const = 0;

    // Queues a press and release of `key` for `window_id`, as if typed (Event::synthetic). UI
    // thread.
    virtual void post_key(std::uint32_t window_id, Key key, KeyModifiers modifiers) = 0;

    // Called on the main thread from inside OS modal loops while the user resizes a window,
    // when the normal event loop is blocked. Must not throw.
    virtual void set_live_resize_callback(LiveResizeCallback callback) = 0;

protected:
    Platform() = default;
};

// Starts the window system (SDL), which the Platform owns until it goes. Call on the main thread,
// which becomes the UI thread. Throws UsageError while another Platform exists.
std::unique_ptr<Platform> create_platform();

// Whether the OS is set to a dark appearance.
bool system_prefers_dark();
// The system's font with its window caption buttons' glyphs (Segoe MDL2 Assets on Windows), or
// nullopt where it has none.
std::optional<std::filesystem::path> caption_icon_font();
// What moves a text field's caret by words: Alt (Option) on macOS, Ctrl elsewhere.
KeyModifiers word_modifier() noexcept;

} // namespace elg::detail::platform

#endif // ELG_PLATFORM_PLATFORM_H
