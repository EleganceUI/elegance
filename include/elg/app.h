// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_APP_H
#define ELG_APP_H

#include <elg/color.h>
#include <elg/export.h>
#include <elg/view.h>

#include <functional>
#include <memory>
#include <string>

namespace elg {

struct WindowDesc {
    std::string title = "Elegence";
    int width = 1280; // logical points
    int height = 720;
    bool resizable = true;
    Color background = Color::white;
};

// Builds a window's root view. Called once, on the UI thread, when the window opens.
using ContentFunction = std::move_only_function<View()>;

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

// Library version string, e.g. "0.1.0".
ELG_API const char* version();

} // namespace elg

#endif // ELG_APP_H
