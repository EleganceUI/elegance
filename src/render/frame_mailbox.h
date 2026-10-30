// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_RENDER_FRAME_MAILBOX_H
#define ELG_RENDER_FRAME_MAILBOX_H

#include "platform/generic/platform.h"
#include "render/display_list.h"
#include "text/atlas.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <variant>
#include <vector>

namespace elg::detail {

// A frame for one window: everything the render thread needs, owned by value.
struct WindowFrame {
    std::uint32_t window_id = 0;
    int pixel_width = 0;
    int pixel_height = 0;
    float scale = 1; // pixels per logical point
    DisplayList list;
};

struct AddWindowCommand {
    std::uint32_t window_id = 0;
    platform::SurfaceSource surface; // the window's; the render thread makes its surface from it
    bool transparent = false;
    void* native_window = nullptr; // HWND on Windows (windows present through the compositor with it)
};

struct RemoveWindowCommand {
    std::uint32_t window_id = 0;
};

// New glyphs for the atlas texture. Posted before any frame that draws them.
struct AtlasUploadCommand {
    std::vector<GlyphUpload> uploads;
};

using RenderCommand = std::variant<AddWindowCommand, RemoveWindowCommand, AtlasUploadCommand>;

// Everything posted since the render thread last took work. Commands run before frames.
struct RenderBatch {
    std::vector<RenderCommand> commands;
    std::vector<WindowFrame> frames;
    std::uint64_t sequence = 0;
};

// The UI → render thread handoff (design.md §15). Frames are coalesced: posting a frame for a
// window replaces that window's pending frame, so a slow GPU never builds up a backlog.
// Every post returns a sequence number that the UI thread can wait on.
class FrameMailbox {
public:
    // --- UI thread ---
    std::uint64_t post(RenderCommand command);
    std::uint64_t post(WindowFrame frame);
    // Waits until the render thread finished everything up to `sequence`.
    // Returns false on timeout.
    bool wait_until_done(std::uint64_t sequence, std::chrono::milliseconds timeout);
    void wait_until_done(std::uint64_t sequence);
    void request_stop();

    // --- Render thread ---
    // Blocks until there is work. Returns false once a stop was requested.
    bool take(RenderBatch& batch);
    // Like take(), but gives up after `timeout`: then `batch` is empty. Returns false once a stop
    // was requested.
    bool take_for(RenderBatch& batch, std::chrono::milliseconds timeout);
    void mark_done(std::uint64_t sequence);
    // The render thread is exiting: drops pending work and releases all waiters.
    void close();

private:
    std::mutex mutex_;
    std::condition_variable work_ready_;
    std::condition_variable work_done_;
    std::vector<RenderCommand> commands_;
    std::vector<WindowFrame> frames_;
    std::uint64_t posted_ = 0;
    std::uint64_t done_ = 0;
    bool stop_ = false;
    bool closed_ = false;
};

} // namespace elg::detail

#endif // ELG_RENDER_FRAME_MAILBOX_H
