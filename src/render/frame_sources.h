// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_FRAME_SOURCES_H
#define ELG_RENDER_FRAME_SOURCES_H

#include "media/video_source.h"
#include "render/frame_mailbox.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace elg::detail {

// The frame sources (the VideoSources of VideoCommands) in each window's last frame, and which of
// their frames each window drew: so each window showing a source draws again when it has a new
// frame, whichever window looked first. Sources count their new frames here: update() is called
// once per source per round, not per window. Render thread.
class FrameSources {
public:
    // Takes the new frames of the sources that `incoming` frames and the visible windows' last
    // frames show: update() once each.
    void update(std::span<const WindowFrame> incoming = {});
    // `frame` was drawn: what it shows is each source's frame as of the last update(). It's kept
    // while it shows a source, to draw again.
    void drew(const WindowFrame& frame);
    void remove(std::uint32_t window_id);
    // Hidden or minimized windows (`visible` false) neither draw again nor have their sources
    // updated, polled or woken for. Windows are visible until told otherwise.
    void set_visible(std::uint32_t window_id, bool visible);

    // The visible windows whose last frame shows a source with a frame newer than the one drawn.
    [[nodiscard]] std::vector<std::uint32_t> stale() const;
    // A window's last frame (kept since it shows a source), to draw again.
    [[nodiscard]] const WindowFrame* frame(std::uint32_t window_id) const;
    // That frame was drawn again with the sources' frames as of the last update().
    void redrew(std::uint32_t window_id);

    // Whether a source in a visible window plays (is_playing_now()) without waking the render
    // thread for its frames (wakes_for_frames()): the render thread then looks for them
    // regularly. Every Linux source wakes it; the Windows and macOS video players and cameras
    // don't yet, and this goes once they do.
    [[nodiscard]] bool polling() const;

private:
    struct Source {
        std::shared_ptr<VideoSource> source;
        std::uint64_t frames = 0; // new frames update() reported
    };
    struct Window {
        std::optional<WindowFrame> frame; // while it shows a source
        bool visible = true;
        std::vector<std::pair<const VideoSource*, std::uint64_t>> drawn; // Source::frames then
    };
    // Each source's frame count as of now, for the sources `frame` shows.
    std::vector<std::pair<const VideoSource*, std::uint64_t>> counts(const WindowFrame& frame);
    // Drops sources no window shows, and tells the rest whether a visible window shows them
    // (VideoSource::set_shown).
    void refresh();

    std::unordered_map<const VideoSource*, Source> sources_;
    std::unordered_map<std::uint32_t, Window> windows_;
};

} // namespace elg::detail

#endif // ELG_RENDER_FRAME_SOURCES_H
