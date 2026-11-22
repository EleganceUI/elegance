// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_PIPELINE_CACHE_BACKEND_H
#define ELG_GPU_PIPELINE_CACHE_BACKEND_H

// GPU's backend interface (ELGSPEC-0001.R4): where the pipeline cache (gpu/pipeline_cache.h) is, as the
// system names the cache folders, implemented in platform/<os>/pipeline_cache.cpp.

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace elg::detail::gpu {

struct PipelineCacheFolders {
    // The folder of the app's own entries, in the user's cache folder, or nullopt when the system names none.
    std::optional<std::filesystem::path> own;
    // The folder of the entries that ship with the app, or nullopt where the system has no such folder.
    std::optional<std::filesystem::path> shipped;
};

// The folders for the app `app`, from the environment variables that `get` reads. Nothing is made or looked at.
PipelineCacheFolders pipeline_cache_folders(std::string_view app,
                                            const std::function<std::optional<std::string>(const char*)>& get);

} // namespace elg::detail::gpu

#endif // ELG_GPU_PIPELINE_CACHE_BACKEND_H
