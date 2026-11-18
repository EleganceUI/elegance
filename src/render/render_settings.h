// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_RENDER_SETTINGS_H
#define ELG_RENDER_RENDER_SETTINGS_H

// Settings of the renderer for tests and debugging (ELGSPEC-0029.R11). Each starts from the
// environment, read once:
// - ELG_FULL_REDRAW=1: every frame is a full redraw of the window, never only its damage.
// - ELG_SHOW_DAMAGE=1: each frame's damage region shows, tinted magenta.
// Tests set them directly. Thread-safe.

#include <atomic>

namespace elg::detail {

struct RenderSettings {
    std::atomic<bool> full_redraw{false};
    std::atomic<bool> show_damage{false};
};

RenderSettings& render_settings();

} // namespace elg::detail

#endif // ELG_RENDER_RENDER_SETTINGS_H
