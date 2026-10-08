// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_PLATFORM_H
#define ELG_PLATFORM_PLATFORM_H

// Internal platform layer (design.md §4). SDL3 is the only backend, but no SDL types leak out
// of platform/*.cpp, so it could be replaced without touching the rest of the framework.

#include <elg/app.h>
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
    system_theme_changed,
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
};

class Window {
public:
    // Created hidden; call show() once the first frame is queued.
    explicit Window(const WindowDesc& desc);
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

    void show();

    // The caller owns the surface and must destroy it before this window.
    [[nodiscard]] VkSurfaceKHR create_surface(VkInstance instance) const;

private:
    SDL_Window* window_ = nullptr;
    std::uint32_t id_ = 0;
};

// Owns SDL's lifetime. Construct on the main thread, which becomes the UI thread.
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
