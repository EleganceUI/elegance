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
// TODO: evict (LRU) or add pages when full.
class GlyphAtlas {
public:
    GlyphAtlas();
    ~GlyphAtlas();
    GlyphAtlas(const GlyphAtlas&) = delete;
    GlyphAtlas& operator=(const GlyphAtlas&) = delete;

    // nullptr for glyphs with no outline (spaces), or when the atlas is full.
    const AtlasGlyph* find_or_add(const FontFace& face, std::uint32_t glyph_id);

    // Glyph pixels finished since the last call; post them before any frame that uses them.
    std::vector<GlyphUpload> take_uploads();
    // Whether glyphs finished since take_uploads() (windows showing them need a frame).
    bool has_uploads() const;
    // Called on a worker thread whenever a glyph finishes (e.g. to wake the event loop).
    void set_on_ready(std::function<void()> on_ready);
    // Waits until every requested glyph is generated, or `timeout` passes; true when done.
    bool wait_idle(std::chrono::milliseconds timeout);
    // Keeps generated glyphs on disk in `directory` between runs (nullopt: don't). Call before
    // the first glyph.
    void set_disk_cache(std::optional<std::filesystem::path> directory);

private:
    std::optional<AtlasGlyph> prepare(const FontFace& face, std::uint32_t glyph_id);
    bool allocate(std::uint32_t width, std::uint32_t height, std::uint32_t& x, std::uint32_t& y);
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
    std::function<void()> on_ready_;
    std::vector<std::jthread> workers_;

    std::uint32_t shelf_x_ = 0;
    std::uint32_t shelf_y_ = 0;
    std::uint32_t shelf_height_ = 0;
    bool reported_full_ = false;
};

} // namespace elg::detail

#endif // ELG_TEXT_GLYPH_ATLAS_H
