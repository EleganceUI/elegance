// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_APP_H
#define ELG_APP_H

#include <elg/color.h>
#include <elg/events.h>
#include <elg/export.h>
#include <elg/move_only_function.h>
#include <elg/view.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace elg {

enum class TitleBar {
    system, // the platform's title bar and frame
    hidden, // none: the content fills the window. Draw your own title bar with
            // .window_drag_area() and WindowControls() (elg/window.h); edges still resize.
    // The content fills the window, and the system's own window controls stay over it, where the
    // platform has them: the traffic lights on macOS (see WindowDesc::traffic_lights_position).
    // Leave room with WindowHandle::system_controls_frame() and drag with .window_drag_area().
    // Elsewhere the window keeps its system title bar (as TitleBar::system).
    integrated,
};

struct WindowDesc {
    std::string title = "Elegance";
    int width = 1280; // logical points
    int height = 720;
    bool resizable = true;
    // Smallest size the user can resize to (logical points).
    std::optional<Size> min_size;
    // Shown where no view draws. Default: the theme's background (ThemeColor::background), or
    // clear for transparent windows.
    std::optional<Color> background;
    TitleBar title_bar = TitleBar::system;
    // macOS, TitleBar::integrated: where the close button's top-left corner sits, in points from
    // the window's top-left corner (the others follow it). Default: where macOS puts them.
    std::optional<Point> traffic_lights_position;
    // The desktop shows through wherever the window's content (and background) isn't opaque:
    // rounded or shaped windows, glassy panels. Usually with TitleBar::hidden. Needs a compositing
    // window system; where transparency isn't available the window stays opaque.
    bool transparent = false;
    // The whole window's opacity, 0...1.
    float opacity = 1;
    // Covers the whole display it opens on, without a frame (borderless, at the desktop's
    // resolution). width/height are its size when it leaves fullscreen.
    bool fullscreen = false;
};

// Builds a window's root view. Called once, on the UI thread, when the window opens.
using ContentFunction = MoveOnlyFunction<View()>;

// Owns the application lifetime: windows, the event loop, and the render thread.
class ELG_API App {
public:
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // app.window({.title = "Hello"}, ContentView) or with a lambda returning a View.
    App& window(WindowDesc desc, ContentFunction content);

    // Opens the windows and runs the event loop until the last window closes or quit() is
    // called. Returns the process exit code.
    // elg::Error and exceptions thrown by app callbacks propagate out of run() after a clean
    // shutdown (design.md §3.2).
    int run();

    // Makes run() return after the current event. Call from the UI thread.
    void quit();

private:
    struct Impl;
    ELG_PRIVATE_MEMBERS_BEGIN
    std::unique_ptr<Impl> impl_;
    ELG_PRIVATE_MEMBERS_END
};

// Runs `task` on the UI thread soon, between events, where state may be written. For results
// of work done on your own threads (parsing, decoding). Call from any thread; tasks run in the
// order posted. Posted while no App runs, it waits for the next one.
ELG_API void post_to_ui_thread(std::function<void()> task);

// Library version string, e.g. "0.1.0".
ELG_API const char* version();

// Sets the mouse pointer's shape until something else sets it. For custom views: set it from
// PointerEvent::Kind::hover events (it can depend on what's under the pointer), and back to
// Cursor::arrow when hover(false) says the pointer left. Call on the UI thread.
ELG_API void set_cursor(Cursor cursor);

} // namespace elg

#endif // ELG_APP_H
