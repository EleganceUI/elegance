// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Desktop integration (design.md §13.4). The dialogs, notifications, and tray need a running App
// (and a person to click), so these check the rules around them.

#include "test_processes.h"

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

using namespace elg;

TEST_CASE("desktop services need a running app", "[desktop]") {
    CHECK_THROWS_AS(choose_files({}, [](const std::vector<std::filesystem::path>&) {}), UsageError);
    CHECK_THROWS_AS(choose_save_file({}, [](const std::optional<std::filesystem::path>&) {}), UsageError);
    CHECK_THROWS_AS(choose_folders({}, [](const std::vector<std::filesystem::path>&) {}), UsageError);
    CHECK_THROWS_AS(notify({.title = "x"}), UsageError);
    CHECK_THROWS_AS(TrayIcon({.tooltip = "x"}), UsageError);
}

#if defined(__linux__)
TEST_CASE("a file dialog still open when the run ends closes, and its callback goes uncalled", "[.gpu][desktop]") {
    // zenity shows the dialog (SDL's driver for it, chosen by the environment): here one that says
    // its process id, then stays open for a minute.
    std::random_device random;
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / std::format("elg-dialog-{:x}", random());
    std::filesystem::create_directories(folder);
    const std::filesystem::path pid_file = folder / "zenity.pid";
    {
        std::ofstream script(folder / "zenity");
        script << "#!/bin/sh\n"
               << "if [ \"$1\" = --version ]; then echo 4.0; exit 0; fi\n"
               << "echo $$ > '" << pid_file.string() << "'\n"
               << "exec sleep 60\n";
    }
    std::filesystem::permissions(folder / "zenity", std::filesystem::perms::owner_all);
    // Before the App starts any thread, and back as it was once they've ended.
    // NOLINTBEGIN(concurrency-mt-unsafe)
    const std::string path = std::getenv("PATH") ? std::getenv("PATH") : "";
    setenv("PATH", (folder.string() + ":" + path).c_str(), 1);
    setenv("SDL_FILE_DIALOG_DRIVER", "zenity", 1);
    // NOLINTEND(concurrency-mt-unsafe)

    // What the callback holds: gone once it's destroyed.
    const auto held = std::make_shared<int>(0);
    bool called = false;
    std::optional<Timer> quit;
    App app;
    app.window({.title = "dialog", .width = 100, .height = 100}, [&] {
        return Text("x").on_appear([&] {
            choose_files({}, [&called, held](const std::vector<std::filesystem::path>&) { called = true; });
            // Once zenity runs, the run ends.
            quit = every(0.05, [&] {
                if (std::filesystem::exists(pid_file)) {
                    app.quit();
                }
            });
        });
    });
    const auto started = std::chrono::steady_clock::now();
    app.run();
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(20));
    CHECK_FALSE(called);
    CHECK(held.use_count() == 1); // the callback, destroyed
    int pid = 0;
    std::ifstream(pid_file) >> pid;
    REQUIRE(pid > 0);
    CHECK(elg_tests::process_ends(pid)); // the dialog closed

    // NOLINTBEGIN(concurrency-mt-unsafe)
    setenv("PATH", path.c_str(), 1);
    unsetenv("SDL_FILE_DIALOG_DRIVER");
    // NOLINTEND(concurrency-mt-unsafe)
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
}
#endif

TEST_CASE("drop targets need a handler", "[desktop]") {
    CHECK_THROWS_AS(Text("x").on_drop({}), UsageError);
    CHECK_NOTHROW(Text("x").on_drop({.files = [](const std::vector<std::filesystem::path>&, Point) {}}));
    CHECK_NOTHROW(Text("x").on_drop({.text = [](const std::string&, Point) {}, .targeted = [](bool) {}}));
    // Drop handlers return nothing; one that returns a value still fits.
    STATIC_REQUIRE(std::is_void_v<decltype(DropOptions::files)::result_type>);
    STATIC_REQUIRE(std::is_void_v<decltype(DropOptions::text)::result_type>);
    CHECK_NOTHROW(Text("x").on_drop({.text = [](const std::string&, Point) { return true; }}));
}
