// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// GPU's backend (gpu/pipeline_cache_backend.h) on Linux and other Unix desktops.

#include "gpu/pipeline_cache_backend.h"

namespace elg::detail::gpu {

PipelineCacheFolders pipeline_cache_folders(std::string_view app,
                                            const std::function<std::optional<std::string>(const char*)>& get) {
    // An empty or relative XDG_CACHE_HOME counts as unset (the XDG Base Directory spec).
    if (const auto xdg = get("XDG_CACHE_HOME"); xdg && path_from_utf8(*xdg).is_absolute()) {
        return {path_from_utf8(*xdg) / "elegance" / path_from_utf8(app) / "pipelines", std::nullopt};
    }
    if (const auto home = get("HOME"); home && !home->empty()) {
        return {path_from_utf8(*home) / ".cache" / "elegance" / path_from_utf8(app) / "pipelines", std::nullopt};
    }
    return {};
}

} // namespace elg::detail::gpu
