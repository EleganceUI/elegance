// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_HOT_RELOAD_H
#define ELG_HOT_RELOAD_H

// Hot reload and previews (design.md §9.6). An app's views live in a views module (a shared
// library); the app shows them with HotReload(), which rebuilds and reloads the module when its
// sources change. State resets on reload; keep data in the host (Stores, the environment).
//
// In the views module:
//   View Root() { return TodoList(); }
//   ELG_MODULE(Root)                      // once per module; ELG_MODULE(nullptr) for previews only
//   ELG_PREVIEW("Empty list") { return TodoList({}); }
//
// In the app (see elegance_hot_reload() in CMake for the options):
//   app.window({.title = "Todos"}, [] { return HotReload({.module = ..., .build_command = ..., .watch = {...}}); });

#include <elg/export.h>
#include <elg/view.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace elg {

// --- In a views module ---

/// A preview: a view shown on its own in the preview host (SwiftUI Previews-style). ELG_PREVIEW
/// registers one.
struct PreviewEntry {
    const char* name; ///< shown in the preview list; it lives in the module
    View (*body)();   ///< builds the preview
    const char* file; ///< the source file of its ELG_PREVIEW
    int line;         ///< the line of its ELG_PREVIEW
};

/// What a views module exports (elg_module_entry). Host and module must be built with the same
/// compiler, runtime, and Elegance (design.md §9.6, rule 4). ELG_MODULE defines it.
struct ModuleEntry {
    std::uint32_t abi = 0; ///< the module_abi the module was built with
    View (*root)() = nullptr; ///< the app's content; nullptr for preview-only modules
    const std::vector<PreviewEntry>* previews = nullptr; ///< the module's previews
};

/// The version of ModuleEntry this Elegance loads. A module built with another one isn't loaded;
/// HotReload() shows an error instead.
inline constexpr std::uint32_t module_abi = 1;

namespace detail {

// The module's previews: an inline function's static, so each module (DLL) has its own.
inline std::vector<PreviewEntry>& module_previews() {
    static std::vector<PreviewEntry> previews;
    return previews;
}

inline bool register_preview(const char* name, View (*body)(), const char* file, int line) {
    module_previews().push_back({name, body, file, line});
    return true;
}

} // namespace detail

#if defined(_WIN32)
#define ELG_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define ELG_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

/// Makes this shared library a views module, with `root` (a View() function, or nullptr) as the
/// app's content.
#define ELG_MODULE(root)                                                                          \
    ELG_MODULE_EXPORT const ::elg::ModuleEntry* elg_module_entry();                               \
    ELG_MODULE_EXPORT const ::elg::ModuleEntry* elg_module_entry() {                              \
        static const ::elg::ModuleEntry entry{::elg::module_abi, root, &::elg::detail::module_previews()}; \
        return &entry;                                                                            \
    }

#define ELG_PREVIEW_CONCAT_(a, b) a##b
#define ELG_PREVIEW_CONCAT(a, b) ELG_PREVIEW_CONCAT_(a, b)
#define ELG_PREVIEW_IMPL_(name, id)                                                                         \
    static ::elg::View id();                                                                                \
    [[maybe_unused]] static const bool ELG_PREVIEW_CONCAT(id, _registered) =                                \
        ::elg::detail::register_preview((name), &(id), __FILE__, __LINE__);                                   \
    static ::elg::View id()

/// Registers a preview of this module, followed by its body:
///
/// @code
/// ELG_PREVIEW("Dark") { return Card().color_scheme(ColorScheme::dark); }
/// @endcode
#define ELG_PREVIEW(name) ELG_PREVIEW_IMPL_(name, ELG_PREVIEW_CONCAT(elg_preview_, __LINE__))

// --- In the host ---

/// What HotReload() shows of the module.
enum class HotReloadShow {
    root,     ///< the module's root view
    previews, ///< a list of the module's previews and the selected one
};

/// The module HotReload() loads, and how it rebuilds it.
struct HotReloadOptions {
    /// The views module as the build writes it. It's loaded from a copy, so the build can
    /// replace it while the app runs.
    std::filesystem::path module;
    /// Run (through the shell) when a watched file changes, e.g.
    /// "cmake --build build --target todo_views". Empty: never rebuild, only reload when the
    /// module file itself changes.
    std::string build_command;
    /// Files and folders (searched recursively) whose changes trigger a rebuild.
    std::vector<std::filesystem::path> watch;
    HotReloadShow show = HotReloadShow::root; ///< the root view, or the previews
};

/// The views module's content, rebuilt and reloaded when its sources change. A small status
/// badge shows building, errors, and reloads. Call it while building views (e.g. as a window's
/// content); the module is loaded right away, or built first if it doesn't exist yet and there's
/// a `build_command`. A background thread checks the watched files a few times a second, and a
/// new module file is loaded once it stops changing. Failures (a build error, a missing or
/// incompatible module) show in the badge rather than throw.
///
/// @param options The module, the build command and what to watch.
/// @return The module's content with the status badge over it.
ELG_API View HotReload(HotReloadOptions options);

} // namespace elg

#endif // ELG_HOT_RELOAD_H
