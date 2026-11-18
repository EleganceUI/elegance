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
#include <elg/headers_hash.h>
#include <elg/view.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace elg {

// --- In a views module ---

/// A preview: a view shown on its own in the preview host (SwiftUI Previews-style), as a
/// ModuleEntry lists it. ELG_PREVIEW registers one.
struct PreviewEntry {
    const char* name; ///< shown in the preview list; the host copies it
    const char* file; ///< the source file of its ELG_PREVIEW
    int line;         ///< the line of its ELG_PREVIEW
};

/// What a views module exports (elg_module_entry). Host and module must be built with the same
/// compiler, runtime, and Elegance (design.md §9.6, rule 4). ELG_MODULE defines it.
struct ModuleEntry {
    std::uint32_t abi = 0; ///< the module_abi the module was built with
    bool has_root = false; ///< whether the module has a root view, the app's content
    const std::vector<PreviewEntry>* previews = nullptr; ///< the module's previews
    /// Builds one of the module's views, with `context`: the root view for `index` -1, else
    /// `previews` at `index`.
    View (*build)(const void* context, std::int32_t index) = nullptr;
    const void* context = nullptr; ///< what `build` is called with: the module's own data
    /// The hash of Elegance's public headers the module was compiled with. HotReload() mounts the
    /// module only when it's the one Elegance was compiled with.
    std::uint64_t elegance_hash = 0;
    /// The app's hash of the types its host shares with the module (layout_hash()), or 0.
    /// HotReload() mounts the module only when it's HotReloadOptions::app_hash.
    std::uint64_t app_hash = 0;
};

/// A hash of the sizes and alignments of `T...`, in order: for the types an app's host shares with
/// its views module (its stores, the values it puts in the environment). Give the same hash to
/// ELG_MODULE_WITH_APP_HASH and HotReloadOptions::app_hash, and a reload that changes those types
/// asks for a restart instead of mounting the module. Any other value both sides compute works too,
/// such as a number the app changes along with those types.
///
/// @tparam T The shared types.
/// @return The hash: 64-bit FNV-1a of each type's size and alignment.
template <class... T>
[[nodiscard]] constexpr std::uint64_t layout_hash() {
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](std::uint64_t value) {
        for (int byte = 0; byte < 8; ++byte) {
            hash = (hash ^ ((value >> (byte * 8)) & 0xFF)) * 1099511628211ULL;
        }
    };
    (..., (mix(sizeof(T)), mix(alignof(T))));
    return hash;
}

/// The version of ModuleEntry this Elegance loads. A module built with another one isn't loaded;
/// HotReload() shows an error instead.
inline constexpr std::uint32_t module_abi = 2;

namespace detail {

// A C++ module's views: its root view, and its previews with their bodies, by index.
struct ModuleViews {
    View (*root)() = nullptr;
    std::vector<PreviewEntry> previews;
    std::vector<View (*)()> bodies;
};

// The module's views: an inline function's static. Each module, and each load of one, has its
// own, since elegance_views_module() exports nothing from a module but its entry.
inline ModuleViews& module_views() {
    static ModuleViews views;
    return views;
}

inline bool register_preview(const char* name, View (*body)(), const char* file, int line) {
    ModuleViews& views = module_views();
    views.previews.push_back({name, file, line});
    views.bodies.push_back(body);
    return true;
}

// ModuleEntry::build in a C++ module: its own views, so it needs no context.
inline View build_module_view(const void*, std::int32_t index) {
    const ModuleViews& views = module_views();
    return index < 0 ? views.root() : views.bodies.at(static_cast<std::size_t>(index))();
}

// The entry ELG_MODULE returns: with the hash of the headers it was compiled with.
inline ModuleEntry module_entry(View (*root)(), std::uint64_t app_hash = 0) {
    ModuleViews& views = module_views();
    views.root = root;
    return {.abi = module_abi,
            .has_root = root != nullptr,
            .previews = &views.previews,
            .build = &build_module_view,
            .elegance_hash = headers_hash,
            .app_hash = app_hash};
}

} // namespace detail

#if defined(_WIN32)
#define ELG_MODULE_EXPORT extern "C" __declspec(dllexport)
#else
#define ELG_MODULE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

/// Makes this shared library a views module, with `root` (a View() function, or nullptr) as the
/// app's content.
#define ELG_MODULE(root) ELG_MODULE_WITH_APP_HASH(root, 0)

/// ELG_MODULE, with `app_hash`: the hash of the types the app's host shares with this module, the
/// one it gives HotReloadOptions::app_hash (see layout_hash()).
#define ELG_MODULE_WITH_APP_HASH(root, app_hash)                                                  \
    ELG_MODULE_EXPORT const ::elg::ModuleEntry* elg_module_entry();                               \
    ELG_MODULE_EXPORT const ::elg::ModuleEntry* elg_module_entry() {                              \
        static const ::elg::ModuleEntry entry = ::elg::detail::module_entry(root, app_hash);      \
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
    /// The hash of the types this host shares with the module, the one the module gives
    /// ELG_MODULE_WITH_APP_HASH (see layout_hash()): a load whose hash differs isn't mounted, and
    /// asks for a restart. 0 for a module that has none.
    std::uint64_t app_hash = 0;
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
