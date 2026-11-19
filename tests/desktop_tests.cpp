// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Desktop integration (design.md §13.4). The dialogs, notifications, and tray need a running App
// (and a person to click), so these check the rules around them.

#include "test_images.h"
#include "test_processes.h"

#include "media/image_decode.h"

#include <elg/elegance.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
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

#if defined(_WIN32)
#include <windows.h>
#include <objbase.h>
#endif

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
    const char* const inherited = std::getenv("PATH");
    const std::string path = inherited ? inherited : "";
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

#if defined(_WIN32)
namespace {

// Whether COM is set up on this thread in a single-threaded apartment (OLE's), or, either way, set
// up explicitly at all (rather than not, or only as part of the process's multithreaded apartment).
struct Apartment {
    bool single_threaded = false;
    bool explicit_setup = false;
};

Apartment this_thread_apartment() {
    APTTYPE type{};
    APTTYPEQUALIFIER qualifier{};
    if (FAILED(CoGetApartmentType(&type, &qualifier))) {
        return {}; // CO_E_NOTINITIALIZED: nothing set COM up anywhere
    }
    const bool single = type == APTTYPE_STA || type == APTTYPE_MAINSTA;
    return {single, single || qualifier != APTTYPEQUALIFIER_IMPLICIT_MTA};
}

} // namespace

TEST_CASE("media used before the run leaves COM on the main thread to the run's OLE", "[.gpu][desktop]") {
    // This thread, which Elegance didn't start, decodes an image and lists audio devices and
    // cameras: WIC, miniaudio and Media Foundation work without COM being set up on it
    // (ELGSPEC-0019.R1).
    const std::vector<std::uint8_t> png = elg::test::black_png(4, 3);
    CHECK(detail::probe_image(png).width == 4);
    (void)audio_output_devices();
    (void)audio_input_devices();
    (void)camera_devices();
    CHECK_FALSE(this_thread_apartment().explicit_setup);

    // So the run can set OLE up on it, in a single-threaded apartment (ELGSPEC-0019.R2).
    Apartment during;
    App app;
    app.window({.title = "com", .width = 100, .height = 100}, [&] {
        return Text("x").on_appear([&] {
            during = this_thread_apartment();
            CHECK(detail::probe_image(png).height == 3); // decoding in that apartment works too
            app.quit();
        });
    });
    app.run();
    CHECK(during.single_threaded);
    CHECK_FALSE(this_thread_apartment().explicit_setup); // only for the run
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
