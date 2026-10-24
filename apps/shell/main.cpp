// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Shell: a desktop environment drawn entirely with Elegence, running fullscreen. A login screen,
// a desktop with movable, resizable windows, a top bar, a dock, a launcher, quick settings and
// notifications, and sample apps: Settings, Files, Terminal, Notes, Calculator, System Monitor.
// Uses only the public API.
//
//   shell                       fullscreen, at the login screen
//   shell --windowed            in a 1600x900 window instead
//   shell --signed-in           skip the login screen
//   shell --open <app>,<app>    open apps at start (settings, files, terminal, notes, calculator,
//                               monitor); implies --signed-in
//   shell --launcher            with the launcher open; --quick-settings likewise
//   shell --light               in the light theme
//   shell --smoke-test          render the first frame, then exit

#include "chrome.h"
#include "model.h"

#include <elg/elegence.h>

#include <cstdio>
#include <sstream>
#include <string>
#include <string_view>

using namespace elg;

int main(int argc, char** argv) {
    bool smoke_test = false;
    bool windowed = false;
    bool signed_in = false;
    bool launcher = false;
    bool quick_settings = false;
    bool light = false;
    std::string open;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        smoke_test |= arg == "--smoke-test";
        windowed |= arg == "--windowed";
        signed_in |= arg == "--signed-in";
        launcher |= arg == "--launcher";
        quick_settings |= arg == "--quick-settings";
        light |= arg == "--light";
        if (arg == "--open" && i + 1 < argc) {
            open = argv[++i];
            signed_in = true;
        }
    }
    try {
        App app;
        shell::Desktop desktop;
        desktop.app = &app;
        const shell::Ticker ticker(desktop);
        app.window({.title = "Elegence Desktop",
                    .width = 1600,
                    .height = 900,
                    .title_bar = windowed ? TitleBar::system : TitleBar::hidden,
                    .fullscreen = !windowed},
                   [&] {
                       if (signed_in) {
                           desktop.locked = false;
                       }
                       std::istringstream apps(open);
                       for (std::string name; std::getline(apps, name, ',');) {
                           for (const shell::AppInfo& info : shell::all_apps()) {
                               std::string lower = info.name;
                               for (char& c : lower) {
                                   c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                               }
                               if (!name.empty() && lower.find(name) != std::string::npos) {
                                   desktop.open(info.kind);
                               }
                           }
                       }
                       desktop.settings.dark = !light;
                       desktop.launcher_open = launcher;
                       desktop.quick_settings_open = quick_settings;
                       if (smoke_test) {
                           app.quit(); // still renders the first frame
                       }
                       return shell::Shell(desktop);
                   });
        return app.run();
    } catch (const elg::Error& e) {
        std::fprintf(stderr, "shell: %s\n", e.what());
        return 1;
    }
}
