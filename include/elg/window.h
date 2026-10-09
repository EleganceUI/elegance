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

#include <elg/environment.h>
#include <elg/export.h>
#include <elg/view.h>

#include <memory>
#include <string_view>

namespace elg {

namespace detail {
struct WindowState;
} // namespace detail

// The window a view is in: Environment<WindowKey>(). The readers are tracked reads, so they can
// drive reactive arguments (e.g. a maximize/restore icon). A default-constructed handle (outside
// any window) does nothing.
class ELG_API WindowHandle {
public:
    WindowHandle() = default;
    explicit WindowHandle(std::shared_ptr<detail::WindowState> state) noexcept : state_(std::move(state)) {}

    // Like the system close button: the window closes (the app quits with its last window).
    void close() const;
    void minimize() const;
    // Maximizes, or restores a maximized window.
    void toggle_maximized() const;

    bool is_maximized() const;
    // Whether the window has keyboard focus (title bars usually dim when it doesn't).
    bool is_active() const;

    friend bool operator==(const WindowHandle& a, const WindowHandle& b) noexcept { return a.state_ == b.state_; }

private:
    ELG_PRIVATE_MEMBERS_BEGIN
    std::shared_ptr<detail::WindowState> state_;
    ELG_PRIVATE_MEMBERS_END
};

struct WindowKey {
    using Value = WindowHandle;
    static constexpr std::string_view name = "elg.window";
    static WindowHandle default_value(const EnvironmentValues&) { return {}; }
};

enum class WindowControlsStyle {
    automatic,      // the platform's: caption buttons on Windows and Linux, traffic lights on macOS
    caption,        // minimize / maximize / close buttons, as on Windows
    traffic_lights, // three colored circles, as on macOS
};

struct WindowControlsOptions {
    WindowControlsStyle style = WindowControlsStyle::automatic;
    bool minimize = true;
    bool maximize = true; // hidden for windows that can't resize? Your choice: set false
};

// Minimize, maximize/restore, and close buttons for a custom title bar. They act on the window
// they're in, and dim while the window isn't active.
ELG_API View WindowControls(WindowControlsOptions options = {});

} // namespace elg

#endif // ELG_WINDOW_H
