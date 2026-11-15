// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_PATH_CACHE_H
#define ELG_RENDER_PATH_CACHE_H

#include "render/display_list.h"

#include <elg/path.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

namespace elg::detail {

// The paths a window's GraphicsContexts drew, flattened, kept from one frame to the next: a path
// drawn again with the same commands, stroke and tolerance is not flattened again, and keeps its
// polygon set (so the renderer recognizes it too). A frame's paint runs under a Use; end_frame()
// then forgets what it didn't draw. UI thread.
class PathCache {
public:
    // Closed polygons of at least three points, and their bounds.
    struct Flattened {
        std::shared_ptr<const Polygons> polygons;
        Rect bounds;
    };

    // While it lives, current() is `cache`.
    class Use {
    public:
        explicit Use(PathCache& cache) noexcept;
        ~Use();
        Use(const Use&) = delete;
        Use& operator=(const Use&) = delete;
        Use(Use&&) = delete;
        Use& operator=(Use&&) = delete;

    private:
        PathCache* previous_;
    };
    // The cache of the paint under way on this thread, or null.
    static PathCache* current() noexcept;

    // `path` flattened within `tolerance` or, with `stroke`, the outline of its stroke: as drawn
    // in the previous frame or earlier in this one, else flattened now.
    Flattened get(const Path& path, const StrokeStyle* stroke, float tolerance);
    // Forgets the paths the frame didn't draw; the next frame starts.
    void end_frame();
    // How many paths it has flattened.
    [[nodiscard]] std::uint64_t flattened() const noexcept { return flattened_; }

    // Flattens with no cache.
    static Flattened flatten(const Path& path, const StrokeStyle* stroke, float tolerance);

private:
    struct Entry {
        Path path;
        std::optional<StrokeStyle> stroke;
        float tolerance = 0;
        Flattened flattened;
        bool used = false; // by the frame being painted
    };
    std::unordered_multimap<std::uint64_t, Entry> entries_; // by a hash of all but `flattened`
    std::uint64_t flattened_ = 0;
};

} // namespace elg::detail

#endif // ELG_RENDER_PATH_CACHE_H
