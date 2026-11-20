// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render/frame_sources.h"

#include "core/profile.h"

#include <algorithm>
#include <unordered_set>
#include <variant>

namespace elg::detail {

namespace {

// The sources `frame` shows, each once.
std::vector<std::shared_ptr<FrameSource>> sources_of(const WindowFrame& frame) {
    std::vector<std::shared_ptr<FrameSource>> found;
    for (const DrawCommand& command : frame.list.commands) {
        const auto* video = std::get_if<VideoCommand>(&command);
        if (video && video->source && std::ranges::find(found, video->source) == found.end()) {
            found.push_back(video->source);
        }
    }
    return found;
}

} // namespace

void FrameSources::update(std::span<const WindowFrame> incoming) {
    ELG_ZONE("Frame sources");
    std::unordered_set<const FrameSource*> updated; // this round: once each
    const auto take = [&](const std::shared_ptr<FrameSource>& source) {
        if (!updated.insert(source.get()).second) {
            return;
        }
        Source& entry = sources_[source.get()];
        entry.source = source;
        if (source->update()) {
            ++entry.frames;
        }
    };
    for (const WindowFrame& frame : incoming) {
        for (const auto& source : sources_of(frame)) {
            take(source);
        }
    }
    for (const auto& [id, window] : windows_) {
        if (window.visible && window.frame) {
            for (const auto& source : sources_of(*window.frame)) {
                take(source);
            }
        }
    }
}

std::vector<std::pair<const FrameSource*, std::uint64_t>> FrameSources::counts(const WindowFrame& frame) {
    std::vector<std::pair<const FrameSource*, std::uint64_t>> result;
    for (const auto& source : sources_of(frame)) {
        Source& entry = sources_[source.get()];
        entry.source = source;
        result.emplace_back(source.get(), entry.frames);
    }
    return result;
}

void FrameSources::drew(const WindowFrame& frame) {
    Window& window = windows_[frame.window_id];
    window.drawn = counts(frame);
    if (window.drawn.empty()) {
        window.frame.reset();
    } else {
        window.frame = frame;
    }
    refresh();
}

void FrameSources::redrew(std::uint32_t window_id) {
    const auto it = windows_.find(window_id);
    if (it == windows_.end()) {
        return;
    }
    if (const std::optional<WindowFrame>& frame = it->second.frame) {
        it->second.drawn = counts(*frame);
    }
}

void FrameSources::remove(std::uint32_t window_id) {
    windows_.erase(window_id);
    refresh();
}

void FrameSources::set_visible(std::uint32_t window_id, bool visible) {
    windows_[window_id].visible = visible;
    refresh();
}

std::vector<std::uint32_t> FrameSources::stale() const {
    std::vector<std::uint32_t> result;
    for (const auto& [id, window] : windows_) {
        if (!window.visible || !window.frame) {
            continue;
        }
        const bool newer = std::ranges::any_of(window.drawn, [&](const auto& drawn) {
            const auto source = sources_.find(drawn.first);
            return source != sources_.end() && source->second.frames != drawn.second;
        });
        if (newer) {
            result.push_back(id);
        }
    }
    return result;
}

const WindowFrame* FrameSources::frame(std::uint32_t window_id) const {
    const auto it = windows_.find(window_id);
    if (it == windows_.end()) {
        return nullptr;
    }
    const std::optional<WindowFrame>& frame = it->second.frame;
    return frame ? &*frame : nullptr;
}

bool FrameSources::polling() const {
    return std::ranges::any_of(windows_, [](const auto& entry) {
        const Window& window = entry.second;
        return window.visible && window.frame &&
               std::ranges::any_of(sources_of(*window.frame), [](const auto& source) {
                   return source->is_playing_now() && !source->wakes_for_frames();
               });
    });
}

void FrameSources::refresh() {
    std::unordered_set<const FrameSource*> kept;
    std::unordered_set<const FrameSource*> shown; // in a visible window
    for (const auto& [id, window] : windows_) {
        for (const auto& [source, count] : window.drawn) {
            kept.insert(source);
            if (window.visible) {
                shown.insert(source);
            }
        }
    }
    for (auto it = sources_.begin(); it != sources_.end();) {
        it->second.source->set_shown(shown.contains(it->first));
        if (kept.contains(it->first)) {
            ++it;
        } else {
            it = sources_.erase(it);
        }
    }
}

} // namespace elg::detail
