// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_TEXT_GLYPH_ATLAS_H
#define ELG_TEXT_GLYPH_ATLAS_H

#include "text/atlas.h"
#include "text/font_library.h"
#include "text/glyph_cache.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace elg::detail {

// Where a glyph is in the atlas and how its quad sits relative to the pen position.
struct AtlasGlyph {
    // Normalized texture coordinates, top-left to bottom-right.
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
    // Quad edges in em units relative to the pen position on the baseline, y up.
    float left = 0, bottom = 0, right = 0, top = 0;
};

struct GlyphJob;

// The UI-side glyph atlas (design.md §15). A new glyph's place and metrics are worked out at once
// on the UI thread (so layout and drawing never wait), and its distance field is generated on a
// pool of worker threads; the pixels are queued for the render thread, which owns the texture.
// Until they arrive the glyph's slot is empty. UI thread only, except where noted.
// When the page is full, the glyphs that don't fit aren't drawn in that frame, and the atlas
// starts over once its uploads are taken: every glyph is placed again as frames draw it (from
// the disk cache, or generated again).
class GlyphAtlas {
public:
    GlyphAtlas();
    ~GlyphAtlas();
    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    // nullptr for glyphs with no outline (spaces), or while the atlas is full.
    const AtlasGlyph* find_or_add(const FontFace& face, std::uint32_t glyph_id);

    // Glyph pixels finished since the last call; post them before any frame that uses them.
    // Between frames: a full atlas starts over here.
    std::vector<GlyphUpload> take_uploads();
    // Whether glyphs finished since take_uploads(), or the atlas started over (windows showing
    // text need a frame).
    bool has_uploads() const;
    // Called on a worker thread whenever a glyph finishes (e.g. to wake the event loop).
    void set_on_ready(std::function<void()> on_ready);
    // Waits until every requested glyph is generated, or `timeout` passes; true when done.
    bool wait_idle(std::chrono::milliseconds timeout);
    // How many glyphs have been sent to the workers so far (UI thread): a frame that raised it
    // draws glyphs that aren't generated yet.
    std::uint64_t requested() const noexcept { return requested_; }
    // Keeps generated glyphs on disk in `directory` between runs (nullopt: don't). Call before
    // the first glyph.
    void set_disk_cache(std::optional<std::filesystem::path> directory);

private:
    enum class Allocation { placed, full, too_large };
    // nullopt with `full` set: no room now (not cached, so it's placed again after starting over).
    std::optional<AtlasGlyph> prepare(const FontFace& face, std::uint32_t glyph_id, bool& full);
    Allocation allocate(std::uint32_t width, std::uint32_t height, std::uint32_t& x, std::uint32_t& y);
    // Forgets every glyph: they're placed again, from the top-left, as frames draw them.
    void start_over();
    void submit(std::unique_ptr<GlyphJob> job);
    void work(std::stop_token stop);

    std::unordered_map<std::uint64_t, std::optional<AtlasGlyph>> glyphs_;
    std::unique_ptr<GlyphDiskCache> disk_; // used by the workers too (it's thread-safe)

    // Shared with the workers.
    mutable std::mutex mutex_;
    std::condition_variable_any wake_workers_;
    std::condition_variable idle_;
    std::deque<std::unique_ptr<GlyphJob>> jobs_;
    std::vector<GlyphUpload> uploads_;
    std::size_t pending_ = 0; // queued or being generated
    // Counts start_over(): pixels of glyphs placed before it are dropped, not uploaded.
    std::uint64_t generation_ = 0;
    bool started_over_ = false; // since the last take_uploads()
    std::function<void()> on_ready_;
    std::vector<std::jthread> workers_;

    std::uint64_t requested_ = 0;
    std::uint32_t shelf_x_ = 0;
    std::uint32_t shelf_y_ = 0;
    std::uint32_t shelf_height_ = 0;
    bool start_over_ = false; // a glyph didn't fit this frame
    std::uint32_t frames_since_start_over_ = 1'000'000;
    bool reported_full_ = false;
    bool reported_too_large_ = false;
};

} // namespace elg::detail

#endif // ELG_TEXT_GLYPH_ATLAS_H
