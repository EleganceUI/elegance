// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_MACOS_APPEARANCE_H
#define ELG_PLATFORM_MACOS_APPEARANCE_H

#include <elg/app.h>

namespace elg::detail::platform::macos {

// Puts a vibrancy view of `material` behind the content of `ns_window` (an NSWindow made
// transparent).
void apply_material(void* ns_window, WindowMaterial material);

} // namespace elg::detail::platform::macos

#endif // ELG_PLATFORM_MACOS_APPEARANCE_H
