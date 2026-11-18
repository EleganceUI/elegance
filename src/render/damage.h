// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_DAMAGE_H
#define ELG_RENDER_DAMAGE_H

// Which part of a window a frame must draw again: the damage region between it and the frame
// before (ELGSPEC-0029.R4, R5), and the drawn area it's merged into (ELGSPEC-0029.R3). Render
// thread; no GPU.

#include "display/display_list.h"

#include <elg/geometry.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace elg::detail {

// What a draw command draws in a frame, and where.
struct Footprint {
    // What it draws: its content, and the layers and clips around it.
    std::uint64_t key = 0;
    // Where it can draw, in window pixels, rounded out: after its layers' transforms, widened by
    // their effects (blurs, shadows), within the window and its clips. Empty for commands that
    // draw nothing themselves (a clip's or a layer's end).
    Rect bounds;
    // What it reads of what's drawn behind it (a backdrop blur, a blend mode), in window pixels.
    std::optional<Rect> reads;

    bool operator==(const Footprint&) const = default;
};

// The footprints of a frame's commands, in painter's order, for a window of `width` x `height`
// pixels.
std::vector<Footprint> footprints(const DisplayList& list, std::uint32_t width, std::uint32_t height);

// The damage region from the frame of `before` to the frame of `now`: the bounds, in both frames,
// of the commands added, removed or changed, a command that moved in painter's order among them;
// then, while it overlaps what an effect of `now` reads, that effect's bounds too.
std::vector<Rect> damage_between(std::span<const Footprint> before, std::span<const Footprint> now);

// The drawn area of a damage region: its rects, overlapping ones merged, then those whose
// bounding box is at most twice their area, then, while more than 4 are left, the two whose
// bounding box adds the least area.
std::vector<Rect> drawn_area(std::vector<Rect> damage);

} // namespace elg::detail

#endif // ELG_RENDER_DAMAGE_H
