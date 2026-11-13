// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_WINDOW_H
#define ELG_WINDOW_H

// The window a view is in, for custom title bars (design.md §4):
//
//   app.window({.title = "Notes", .title_bar = TitleBar::hidden}, [] {
//       return VStack({.spacing = 0},
//                     HStack(Text("Notes"), Spacer(), WindowControls())
//                         .frame({.height = 32})
//                         .window_drag_area(),
//                     Content());
//   });

#include <elg/app.h>
#include <elg/environment.h>
#include <elg/export.h>
#include <elg/view.h>

#include <memory>
#include <string_view>

namespace elg {

namespace detail {
struct WindowState;
} // namespace detail

/// The window a view is in: Environment<WindowKey>(). The readers are tracked reads, so they can
/// drive reactive arguments (e.g. a maximize/restore icon). A default-constructed handle (outside
/// any window) does nothing, and neither does one kept after its window closed. Handles compare
/// equal when they refer to the same window. UI thread only.
class ELG_API WindowHandle {
public:
    WindowHandle() = default;
    /// A handle to a window's state. The framework makes these; read them with
    /// Environment<WindowKey>().
    ///
    /// @param state The window's state, shared by its handles.
    explicit WindowHandle(std::shared_ptr<detail::WindowState> state) noexcept : state_(std::move(state)) {}

    /// Like the system close button: the window closes (the app quits with its last window).
    void close() const;
    /// Minimizes the window.
    void minimize() const;
    /// Maximizes, or restores a maximized window.
    void toggle_maximized() const;

    /// Whether the window is maximized. A tracked read.
    ///
    /// @return true while it's maximized; false for a default-constructed handle.
    [[nodiscard]] bool is_maximized() const;
    /// Whether the window has keyboard focus (title bars usually dim when it doesn't). A tracked
    /// read.
    ///
    /// @return true while it's the active window, and for a default-constructed handle.
    [[nodiscard]] bool is_active() const;
    /// The window's WindowDesc::title_bar.
    ///
    /// @return Its title bar; TitleBar::system for a default-constructed handle.
    [[nodiscard]] TitleBar title_bar() const;
    /// Where the system draws its window controls over the content (the traffic lights of a
    /// TitleBar::integrated window on macOS), in the window's logical points. Empty where it
    /// doesn't (other title bars and platforms, fullscreen). Keep content clear of it:
    ///
    /// @code
    /// VStack(EmptyView().frame([=] { return Size{0, window().system_controls_frame().max_y()}; }),
    ///        content)
    /// @endcode
    ///
    /// A tracked read: it changes as the window enters and leaves fullscreen.
    ///
    /// @return The controls' frame, or an empty Rect.
    [[nodiscard]] Rect system_controls_frame() const;

    friend bool operator==(const WindowHandle& a, const WindowHandle& b) noexcept { return a.state_ == b.state_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::WindowState> state_;
    ELG_PRIVATE_MEMBERS_END
};

/// The window the views inside are in. Each window sets it; read it with
/// Environment<WindowKey>().
struct WindowKey {
    /// What the key holds.
    using Value = WindowHandle;
    static constexpr std::string_view name = "elg.window"; ///< Its stable, unique name.
    /// Used outside any window.
    /// @return A default-constructed handle, which does nothing.
    static WindowHandle default_value(const EnvironmentValues&) { return {}; }
};

/// What WindowControls() look like.
enum class WindowControlsStyle {
    automatic,      ///< the platform's: caption buttons on Windows and Linux, traffic lights on macOS
    caption,        ///< minimize / maximize / close buttons, as on Windows
    traffic_lights, ///< three colored circles, as on macOS
};

/// Which WindowControls() show, and how they look. The close button always shows.
struct WindowControlsOptions {
    WindowControlsStyle style = WindowControlsStyle::automatic; ///< how they look
    bool minimize = true; ///< show the minimize button
    /// Show the maximize/restore button. It isn't hidden for windows that can't resize: set false
    /// for those.
    bool maximize = true;
};

/// Minimize, maximize/restore, and close buttons for a custom title bar. They act on the window
/// they're in, and dim while the window isn't active. They never take keyboard focus.
/// With WindowControlsStyle::automatic they show only in TitleBar::hidden windows: wherever the
/// system draws its own controls (TitleBar::system and integrated), this is empty, so the same
/// layout works with either.
///
/// @param options Which buttons show, and their style.
/// @return The buttons in a row, or an empty view.
ELG_API View WindowControls(WindowControlsOptions options = {});

} // namespace elg

#endif // ELG_WINDOW_H
