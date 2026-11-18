// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_SOURCE_HOST_H
#define ELG_MEDIA_SOURCE_HOST_H

// How the sources that an OS's engine runs (videos and cameras) are put together: the VideoSource
// keeps the states views read and owns an EngineBox (the engine, and the SourceHost it reports
// to) and the EngineFrames that display lists hold.

#include "media/frame_source.h"
#include "media/video_backend.h"
#include "media/video_source.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace elg::detail {

// The FrameSourceHost of an OS's engine: what the engine reports goes to the source's frames, and
// to its VideoSource's states until that goes (detach()).
class SourceHost final : public FrameSourceHost {
public:
    SourceHost(VideoSource& source, std::shared_ptr<FrameSource> frames) noexcept
        : source_(&source), frames_(std::move(frames)) {}
    SourceHost(const SourceHost&) = delete;
    SourceHost& operator=(const SourceHost&) = delete;
    SourceHost(SourceHost&&) = delete;
    SourceHost& operator=(SourceHost&&) = delete;
    ~SourceHost() override = default;

    // UI thread: the VideoSource goes. What the engine reports to the states does nothing from
    // now on, and the getters give the states' first values.
    void detach() noexcept { source_ = nullptr; }

    [[nodiscard]] std::uint64_t source_id() const noexcept override;
    [[nodiscard]] std::uint64_t new_id() const override;
    void set_playing_now(bool playing) noexcept override;
    [[nodiscard]] bool is_playing_now() const noexcept override;
    void notify_new_frame() const override;
    [[nodiscard]] std::optional<RenderAdapter> render_adapter() const override;
    [[nodiscard]] bool imports_native_frames() const override;
    [[nodiscard]] std::optional<VideoFrame> import_native_frame(const VideoFrame& frame) override;
    [[nodiscard]] bool imported_frame_current(const VideoFrame& frame) const override;
    [[nodiscard]] std::shared_ptr<const DecodedImage> decode_image(
        std::span<const std::uint8_t> data) const override;

    void set_playing(bool playing) override;
    [[nodiscard]] bool playing() const override;
    void set_ready() override;
    void set_time(double seconds) override;
    void set_duration(double seconds) override;
    [[nodiscard]] double duration() const override;
    void set_size(float width, float height) override;
    void set_volume(float volume) override;
    void set_muted(bool muted) override;
    void set_looping(bool looping) override;
    [[nodiscard]] bool looping() const override;
    void set_error(std::string message) override;

private:
    VideoSource* source_; // UI thread
    const std::shared_ptr<FrameSource> frames_;
};

// An OS's engine and the host it reports to, which outlives it. Its VideoSource owns it, and the
// render thread holds it only while it asks the engine for a frame. Destroyed on the UI thread
// (make_ui_shared()): the engine's callbacks and posted tasks find its host weakly.
template <class Engine>
struct EngineBox {
    EngineBox(VideoSource& source, std::shared_ptr<FrameSource> frames, std::unique_ptr<Engine> os_engine)
        : host(source, std::move(frames)), engine(std::move(os_engine)) {}

    // The host, as the engine holds it: sharing the box's lifetime.
    static std::shared_ptr<FrameSourceHost> host_of(const std::shared_ptr<EngineBox>& box) {
        return {box, &box->host};
    }

    SourceHost host;
    const std::unique_ptr<Engine> engine;
};

// The frames of a source an OS's engine runs. They ask the engine through a weak reference, so a
// display list that holds them doesn't keep the engine. Once its VideoSource has let it go, there
// are no new frames, and current() gives the frame it gave last.
template <class Engine>
class EngineFrames final : public FrameSource {
public:
    explicit EngineFrames(bool wakes_for_frames) : FrameSource(wakes_for_frames) {}

    // UI thread, before the frames are shown.
    void attach(std::weak_ptr<EngineBox<Engine>> box) { box_ = std::move(box); }

    bool update() override {
        const std::shared_ptr<EngineBox<Engine>> box = box_.lock();
        return box && box->engine->update();
    }

    [[nodiscard]] std::optional<VideoFrame> current() const override {
        if (const std::shared_ptr<EngineBox<Engine>> box = box_.lock()) {
            std::optional<VideoFrame> frame = box->engine->current();
            const std::lock_guard lock(mutex_);
            last_ = frame;
            return frame;
        }
        const std::lock_guard lock(mutex_);
        return last_;
    }

private:
    std::weak_ptr<EngineBox<Engine>> box_;
    mutable std::mutex mutex_;
    mutable std::optional<VideoFrame> last_;
};

} // namespace elg::detail

#endif // ELG_MEDIA_SOURCE_HOST_H
