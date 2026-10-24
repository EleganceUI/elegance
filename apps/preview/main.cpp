// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// elegance-preview: shows a views module's previews (or its root view) and reloads them when
// its sources change (design.md §9.6).
//
//   elegance-preview                       the gallery module built with it
//   elegance-preview --root                the gallery's root view instead of its previews
//   elegance-preview --module <file> [--build "<command>"] [--watch <folder>]...

#include "elegance-preview_hot_reload.h"

#include <elg/elegance.h>

#include <cstdio>
#include <string_view>

int main(int argc, char** argv) {
    elg::HotReloadOptions options = elg_hot_reload_options();
    options.show = elg::HotReloadShow::previews;
    bool own_module = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--root") {
            options.show = elg::HotReloadShow::root;
        } else if (arg == "--smoke-test") {
            // handled below
        } else if (i + 1 < argc && arg == "--module") {
            if (!own_module) {
                own_module = true;
                options.build_command.clear();
                options.watch.clear();
            }
            options.module = argv[++i];
        } else if (i + 1 < argc && arg == "--build") {
            options.build_command = argv[++i];
        } else if (i + 1 < argc && arg == "--watch") {
            options.watch.emplace_back(argv[++i]);
        } else {
            std::fprintf(stderr, "usage: elegance-preview [--root] [--module <file> [--build \"<command>\"] [--watch <folder>]...]\n");
            return 2;
        }
    }
    bool smoke_test = false;
    for (int i = 1; i < argc; ++i) {
        smoke_test |= std::string_view(argv[i]) == "--smoke-test";
    }

    try {
        elg::App app;
        const std::string title = "Preview — " + options.module.stem().string();
        app.window({.title = title, .width = 960, .height = 680}, [&] {
            if (smoke_test) {
                app.quit();
            }
            return elg::HotReload(options);
        });
        return app.run();
    } catch (const elg::Error& e) {
        std::fprintf(stderr, "elegance-preview: %s\n", e.what());
        return 1;
    }
}
