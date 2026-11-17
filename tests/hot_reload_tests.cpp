// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Hot reload: views modules, their entries, and the host that loads them (HotReload()). The
// modules (tests/hot_reload) are built with elegance_views_module(), as an app's are; only shared
// builds have them.

#include "core/dynamic_library.h"
#include "core/ui_tasks.h"
#include "test_processes.h"
#include "text/text_system.h"
#include "view/element.h"
#include "view/view_node.h"

#include <elg/hot_reload.h>
#include <elg/reactive.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

using namespace elg;
namespace fs = std::filesystem;

namespace {

// A fresh folder for copies of the modules, removed afterwards. Each load is of a copy of its own,
// as the host's are: the dynamic linker would hand out the library already loaded from a file.
struct TemporaryDirectory {
    fs::path path;
    TemporaryDirectory() {
        std::random_device random;
        path = fs::temp_directory_path() / std::format("elg-hot-reload-test-{:x}", random());
        fs::create_directories(path);
    }
    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(TemporaryDirectory&&) = delete;
    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path, ignored);
    }

    // A copy of `module`, as `name` (with the module's extension).
    [[nodiscard]] fs::path copy(const fs::path& module, const std::string& name) const {
        const fs::path to = path / (name + module.extension().string());
        fs::copy_file(module, to);
        return to;
    }
};

using EntryFunction = const ModuleEntry* (*)();

const ModuleEntry& entry_of(void* entry_function) {
    REQUIRE(entry_function);
    const ModuleEntry* entry = reinterpret_cast<EntryFunction>(entry_function)();
    REQUIRE(entry);
    REQUIRE(entry->abi == module_abi);
    return *entry;
}

detail::TextSystem& text_system() {
    static detail::TextSystem system;
    return system;
}

// The texts that `root` and the elements inside it show.
std::vector<std::string> texts(detail::Element& root) {
    std::vector<std::string> found;
    const auto visit = [&](this const auto& self, detail::Element& element) -> void {
        detail::Inspection inspection;
        element.inspect(inspection);
        if (inspection.name == "Text") {
            found.push_back(inspection.detail);
        }
        element.for_each_child([&](detail::Element& child) { self(child); });
    };
    visit(root);
    return found;
}

// The texts a view shows.
std::vector<std::string> texts(const View& view) {
    detail::Scope scope;
    const detail::Scope::Use use(scope);
    const auto root = detail::lower(view, {.text = &text_system()});
    return texts(*root);
}

// A view mounted with a scope of its own, as a window's content is.
struct Mounted {
    explicit Mounted(const View& view) : root(detail::lower(view, {.text = &text_system()})) {}
    detail::Scope scope;
    detail::Scope::Use use{scope};
    std::unique_ptr<detail::Element> root;

    // Runs what other threads posted to the UI thread, and the effects that changed.
    static void update() {
        detail::run_ui_tasks();
        detail::flush_effects();
    }
    [[nodiscard]] bool shows(const std::string& text) const { return std::ranges::contains(texts(*root), text); }
};

std::vector<std::string> preview_names(const ModuleEntry& entry) {
    std::vector<std::string> names;
    if (entry.previews) {
        for (const PreviewEntry& preview : *entry.previews) {
            names.emplace_back(preview.name);
        }
    }
    return names;
}

} // namespace

TEST_CASE("each load of a module lists only its own previews, though another load's symbols are global",
          "[hot-reload]") {
    const TemporaryDirectory folder;
    // The first load's symbols go where the dynamic linker looks first for every later load's,
    // as it does for STB_GNU_UNIQUE symbols (GCC's inline statics): a module exporting its
    // preview registry would share the first load's.
#if defined(_WIN32)
    const detail::DynamicLibrary first(folder.copy(ELG_TEST_FIRST_MODULE, "first"));
    const ModuleEntry& first_entry = entry_of(first.symbol("elg_module_entry"));
#else
    void* const first = dlopen(folder.copy(ELG_TEST_FIRST_MODULE, "first").c_str(), RTLD_NOW | RTLD_GLOBAL);
    REQUIRE(first);
    const ModuleEntry& first_entry = entry_of(dlsym(first, "elg_module_entry"));
#endif
    const detail::DynamicLibrary again(folder.copy(ELG_TEST_FIRST_MODULE, "again"));
    const detail::DynamicLibrary second(folder.copy(ELG_TEST_SECOND_MODULE, "second"));
    const ModuleEntry& again_entry = entry_of(again.symbol("elg_module_entry"));
    const ModuleEntry& second_entry = entry_of(second.symbol("elg_module_entry"));

    CHECK(preview_names(first_entry) == std::vector<std::string>{"first: one", "first: two"});
    // Its statics were initialized for this load: its registry has its previews once.
    CHECK(preview_names(again_entry) == std::vector<std::string>{"first: one", "first: two"});
    CHECK(again_entry.previews != first_entry.previews);
    CHECK(preview_names(second_entry) == std::vector<std::string>{"second: one"});
    // Nothing but the entry is exported (the Itanium ABI's name of detail::module_views()).
    CHECK_FALSE(again.symbol("_ZN3elg6detail12module_viewsEv"));
#if !defined(_WIN32)
    dlclose(first);
#endif
}

TEST_CASE("each module's entry builds that module's views", "[hot-reload]") {
    const TemporaryDirectory folder;
    const detail::DynamicLibrary first(folder.copy(ELG_TEST_FIRST_MODULE, "first"));
    const detail::DynamicLibrary second(folder.copy(ELG_TEST_SECOND_MODULE, "second"));
    const ModuleEntry& first_entry = entry_of(first.symbol("elg_module_entry"));
    const ModuleEntry& second_entry = entry_of(second.symbol("elg_module_entry"));
    REQUIRE(first_entry.has_root);
    REQUIRE(second_entry.has_root);
    CHECK(texts(first_entry.build(first_entry.context, -1)) == std::vector<std::string>{"first root"});
    CHECK(texts(second_entry.build(second_entry.context, -1)) == std::vector<std::string>{"second root"});
    CHECK(texts(first_entry.build(first_entry.context, 1)) == std::vector<std::string>{"first: two"});
    CHECK(texts(second_entry.build(second_entry.context, 0)) == std::vector<std::string>{"second: one"});
}

#if !defined(_WIN32)
TEST_CASE("a host destroyed while its module builds ends the build at once, and what the build started",
          "[hot-reload]") {
    using namespace std::chrono_literals;
    const TemporaryDirectory folder;
    const fs::path ids = folder.path / "build-ids";
    // The module isn't built yet, so the host builds it right away. The build's shell starts a
    // sleep, which holds the build's output open, writes their process ids, and waits for it.
    HotReloadOptions options;
    options.module = folder.path / "views";
    options.build_command = std::format("sleep 30 & echo $$ $! > '{0}.part' && mv '{0}.part' '{0}'; wait", ids.string());
    auto mounted = std::make_unique<Mounted>(HotReload(options));

    // The UI thread goes on while the build runs.
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    while (!fs::exists(ids) && std::chrono::steady_clock::now() < deadline) {
        mounted->update();
        std::this_thread::sleep_for(10ms);
    }
    REQUIRE(fs::exists(ids));
    int shell = 0;
    int sleep = 0;
    std::ifstream(ids) >> shell >> sleep;
    REQUIRE(shell > 0);
    REQUIRE(sleep > 0);
    mounted->update();
    CHECK(mounted->shows("Building…"));

    // As a closed window's views go, so does the host that they hold.
    const auto stopping = std::chrono::steady_clock::now();
    mounted.reset();
    CHECK(std::chrono::steady_clock::now() - stopping < 2s);
    CHECK(elg_tests::process_ends(shell));
    CHECK(elg_tests::process_ends(sleep));
    detail::run_ui_tasks(); // what the build's thread posted for the host that's gone
}
#endif
