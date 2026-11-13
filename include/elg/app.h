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
#include <vector>

namespace elg {

/// The window's frame and title bar (WindowDesc::title_bar).
enum class TitleBar {
    system, ///< the platform's title bar and frame
    hidden, ///< none: the content fills the window. Draw your own title bar with
            ///< .window_drag_area() and WindowControls() (elg/window.h); edges still resize.
    /// The content fills the window, and the system's own window controls stay over it, where the
    /// platform has them: the traffic lights on macOS (see WindowDesc::system_controls_position).
    /// Leave room with WindowHandle::system_controls_frame() and drag with .window_drag_area().
    /// Elsewhere the window keeps its system title bar (as TitleBar::system).
    integrated,
};

/// A backdrop the system draws behind the window's content: the desktop and windows below,
/// blurred and tinted. Where the content (and background) isn't opaque, it shows through, as with
/// WindowDesc::transparent. Each platform uses its nearest match:
/// @verbatim
///              macOS (vibrancy)         Windows 11 (22H2 and later)
///   sidebar        sidebar                  Mica Alt (tabbed)
///   under_window   under-window background  Mica
///   mica           under-window background  Mica
///   acrylic        HUD window               Acrylic
/// @endverbatim
/// Elsewhere (Windows 10, Linux) the window stays opaque and draws its background as usual.
enum class WindowMaterial { none, sidebar, under_window, mica, acrylic };

/// How App::window() opens a window. Sizes are in logical points.
struct WindowDesc {
    std::string title = "Elegance"; ///< the title bar's text
    int width = 1280; ///< logical points; must be positive
    int height = 720; ///< logical points; must be positive
    bool resizable = true; ///< whether the user can resize it
    /// Smallest size the user can resize to (logical points).
    std::optional<Size> min_size;
    /// Shown where no view draws. Default: the theme's background (ThemeColor::background), or
    /// clear for transparent windows.
    std::optional<Color> background;
    TitleBar title_bar = TitleBar::system; ///< the frame and title bar
    /// TitleBar::integrated: where the system's window controls go, as the first one's top-left
    /// corner in points from the window's top-left corner (the others follow it). Default: where
    /// the system puts them. Only macOS draws its controls over the content, so only macOS uses it.
    std::optional<Point> system_controls_position;
    /// The desktop shows through wherever the window's content (and background) isn't opaque:
    /// rounded or shaped windows, glassy panels. Usually with TitleBar::hidden. Needs a compositing
    /// window system; where transparency isn't available the window stays opaque.
    bool transparent = false;
    /// The system's backdrop behind the content (see WindowMaterial). With one, the default
    /// background is clear where the platform draws it.
    WindowMaterial material = WindowMaterial::none;
    /// The whole window's opacity, 0...1.
    float opacity = 1;
    /// Covers the whole display it opens on, without a frame (borderless, at the desktop's
    /// resolution). width/height are its size when it leaves fullscreen.
    bool fullscreen = false;
};

// --- Menu bar ---

/// A key combination, e.g. {Key::s} for Cmd+S on macOS and Ctrl+S elsewhere, or
/// {Key::s, primary_modifier() | KeyModifiers::shift}.
struct KeyboardShortcut {
    Key key = Key::unknown; ///< the key, by meaning
    KeyModifiers modifiers = primary_modifier(); ///< held with it; default: Cmd on macOS, Ctrl elsewhere
    bool operator==(const KeyboardShortcut&) const = default;
};

/// What a menu item is to the system. Items with a role do the standard thing unless they have
/// an action of their own, and take the standard title and shortcut if they have none.
enum class MenuRole {
    none, ///< an ordinary item or menu
    // Top-level menus.
    app,    ///< macOS's application menu (titled with the app's name; elsewhere its items join the
            ///< first menu)
    window, ///< macOS lists the open windows in it
    help,   ///< macOS adds its search field
    // Items.
    about,        ///< macOS: the standard About panel
    settings,     ///< Cmd+, on macOS
    quit,         ///< App::quit()
    hide,         ///< macOS only (left out elsewhere): hide the app,
    hide_others,  ///< the other apps,
    show_all,     ///< or show them all again
    undo,         ///< these send their shortcut to the focused view (a text field)
    redo,
    cut,
    copy,
    paste,
    select_all,
    minimize,          ///< the active window
    zoom,              ///< maximize or restore it
    toggle_fullscreen, ///< macOS only
};

/// An item of the menu bar: a top-level menu (with a submenu), a command, a check box, or a
/// separator (an empty title and no role).
struct MenuBarItem {
    std::string title; ///< empty: the role's standard title (or, with no role, a separator)
    std::function<void()> action;                  ///< when chosen
    std::optional<bool> checked;                   ///< a check box, initially checked or not
    std::function<void(bool)> toggled;             ///< check boxes: when chosen, with the new state
    bool enabled = true; ///< false: grayed out, and its shortcut does nothing
    std::optional<KeyboardShortcut> shortcut;      ///< also works while the menu is closed
    MenuRole role = MenuRole::none; ///< what it is to the system
    std::vector<MenuBarItem> submenu; ///< a menu's items
};

/// Builds a window's root view. Called once, on the UI thread, when the window opens.
using ContentFunction = MoveOnlyFunction<View()>;

/// Owns the application lifetime: windows, the event loop, and the render thread.
///
/// @code
/// App app;
/// app.window({.title = "Hello"}, [] { return Text("Hello"); });
/// return app.run();
/// @endcode
class ELG_API App {
public:
    /// An app with no windows yet. Nothing opens until run().
    App();
    ~App();

    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    /// Adds a window, which opens when run() starts:
    ///
    /// @code
    /// app.window({.title = "Hello"}, ContentView) // or with a lambda returning a View
    /// @endcode
    ///
    /// @param desc Its title, size, title bar and the like.
    /// @param content Builds its root view; must not be empty.
    /// @return This app, for chaining.
    /// @throws UsageError While the app runs (adding windows then isn't supported yet), or when
    ///   `content` is empty.
    App& window(WindowDesc desc, ContentFunction content);

    /// The menu bar: top-level menus with their items (design.md §13.4). macOS: the app's menu bar
    /// at the top of the screen (without one, a minimal app menu). Windows: a menu bar in each
    /// window with a system title bar. Linux: a bar of menus drawn at the top of each window. Its
    /// shortcuts work while a window of the app has the keyboard and no view took the key.
    /// Callable while the app runs, replacing the menus. Item callbacks run on the UI thread.
    ///
    /// @param menus The top-level menus, in order, each with its items as `submenu`.
    /// @return This app, for chaining.
    App& menu_bar(std::vector<MenuBarItem> menus);

    /// Opens the windows and runs the event loop until the last window closes or quit() is
    /// called. The calling thread becomes the UI thread until it returns.
    /// elg::Error and exceptions thrown by app callbacks propagate out of run() after a clean
    /// shutdown (design.md §3.2).
    ///
    /// @return The process exit code: 0.
    /// @throws UsageError When the app is already running, has no windows, or a window's
    ///   WindowDesc is invalid (a size that isn't positive).
    int run();

    /// Makes run() return after the current event. Call from the UI thread. Before run(), it has
    /// no effect.
    void quit();

private:
    struct Impl;
    ELG_PRIVATE_MEMBERS_BEGIN
    std::unique_ptr<Impl> impl_;
    ELG_PRIVATE_MEMBERS_END
};

/// Runs `task` on the UI thread soon, between events, where state may be written. For results
/// of work done on your own threads (parsing, decoding). Call from any thread; tasks run in the
/// order posted. Posted while no App runs, it waits for the next one.
///
/// @param task The work to run; it's moved to the UI thread.
ELG_API void post_to_ui_thread(std::function<void()> task);

/// Library version string.
///
/// @return The version, e.g. "0.1.0": a static string, never null.
ELG_API const char* version();

} // namespace elg

#endif // ELG_APP_H
