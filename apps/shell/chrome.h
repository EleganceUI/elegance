// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef SHELL_CHROME_H
#define SHELL_CHROME_H

// The shell: the login screen, the desktop with its windows, the top bar and dock, the launcher, quick
// settings and notifications. Plus a few building blocks the apps share.

#include "model.h"

#include <elg/elegance.h>

#include <functional>

namespace shell {

// The whole screen: the login screen while locked, else the desktop.
elg::View Shell(const Desktop& desktop);

// The window's content for its app (apps.cpp).
elg::View AppContent(const Desktop& desktop, const Window& window);

// --- Shared pieces ---

// A gradient tile with the app's icon, as in the launcher and dock.
elg::View AppIcon(AppKind app, float size);

// Buttons that light up under the pointer: rows, toolbar icons, tiles.
class HoverButtonStyle final : public elg::ButtonStyle {
public:
    explicit HoverButtonStyle(float corner_radius = 8, std::function<bool()> selected = {})
        : corner_radius_(corner_radius), selected_(std::move(selected)) {}
    elg::View make_body(const elg::ButtonConfiguration& button) const override;

private:
    float corner_radius_;
    std::function<bool()> selected_;
};

// Pressing and dragging: `began` on the press, `changed` with the pointer's movement since then,
// `ended` on release. A double click calls `double_clicked` instead of starting a drag.
struct DragActions {
    std::function<void()> began;
    std::function<void(elg::Point)> changed;
    std::function<void()> ended;
    std::function<void()> double_clicked;
};
elg::View Draggable(elg::View content, DragActions actions);

// A gradient for wallpapers and tiles.
elg::LinearGradient Gradient(const std::vector<elg::Color>& colors,
                             elg::UnitPoint start = elg::UnitPoint::top_leading,
                             elg::UnitPoint end = elg::UnitPoint::bottom_trailing);

} // namespace shell

#endif // SHELL_CHROME_H
