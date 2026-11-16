// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_MEDIA_SOURCE_HOST_H
#define ELG_MEDIA_SOURCE_HOST_H

#include "media/video_backend.h"
#include "media/video_source.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace elg::detail {

// The FrameSourceHost of a VideoSource whose frames an OS's engine makes: what the engine reports
// goes to the source's state. A member of the source, declared before the engine it outlives.
class SourceHost final : public FrameSourceHost {
public:
    explicit SourceHost(VideoSource& source) noexcept : source_(source) {}
    SourceHost(const SourceHost&) = delete;
    SourceHost& operator=(const SourceHost&) = delete;
    SourceHost(SourceHost&&) = delete;
    SourceHost& operator=(SourceHost&&) = delete;
    ~SourceHost() override = default;

    [[nodiscard]] std::uint64_t source_id() const noexcept override;
    [[nodiscard]] std::uint64_t new_id() const override;
    void set_playing_now(bool playing) noexcept override;
    [[nodiscard]] bool is_playing_now() const noexcept override;
    void notify_new_frame() const override;
    [[nodiscard]] std::optional<RenderAdapter> render_adapter() const override;
    [[nodiscard]] bool imports_native_frames() const override;
    [[nodiscard]] std::optional<VideoFrame> import_native_frame(const VideoFrame& frame) override;
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
    VideoSource& source_;
};

} // namespace elg::detail

#endif // ELG_MEDIA_SOURCE_HOST_H
