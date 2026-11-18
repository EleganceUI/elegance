// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render/render_settings.h"

#include "core/os_backend.h"

namespace elg::detail {

namespace {

bool on(const char* variable) {
    return os::environment_variable(variable).value_or("") == "1";
}

} // namespace

RenderSettings& render_settings() {
    static RenderSettings settings{on("ELG_FULL_REDRAW"), on("ELG_SHOW_DAMAGE")};
    return settings;
}

} // namespace elg::detail
