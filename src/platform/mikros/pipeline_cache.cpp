// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// GPU's backend (gpu/pipeline_cache_backend.h) on Mikros: the app's cache folder is XDG_CACHE_HOME, and its
// package is in `package` in HOME, with its pipelines in `pipelines` there (ELGSPEC-0040.R9,
// ELGSPEC-0040.R20). An app that isn't installed has neither, and gets no cache. Paths on Mikros start with '/',
// whichever system builds this.

#include "gpu/pipeline_cache_backend.h"

namespace elg::detail::gpu {

namespace {

std::optional<std::filesystem::path> path_on_mikros(const std::optional<std::string>& path) {
    if (path && !path->empty() && path->front() == '/') {
        return path_from_utf8(*path);
    }
    return std::nullopt;
}

} // namespace

PipelineCacheFolders pipeline_cache_folders(std::string_view /*app*/,
                                            const std::function<std::optional<std::string>(const char*)>& get) {
    PipelineCacheFolders folders;
    if (const auto cache = path_on_mikros(get("XDG_CACHE_HOME"))) {
        folders.own = *cache / "elegance" / "pipelines";
    }
    if (const auto home = path_on_mikros(get("HOME"))) {
        folders.shipped = *home / "package" / "pipelines";
    }
    return folders;
}

} // namespace elg::detail::gpu
