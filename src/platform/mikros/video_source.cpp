// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Videos on Mikros (media/video_backend.h), which has no video playback: the engine that a
// VideoSource gets is made, and says so in error(); playing it fails with the error of
// platform/mikros/unavailable.h. The rest of what views read follows what was set, as for a
// video that never plays.

#include "media/video_backend.h"

#include "platform/mikros/unavailable.h"

#include <memory>
#include <string>

namespace elg::detail {

namespace {

class UnavailableVideoEngine final : public VideoEngine {
public:
    [[nodiscard]] bool wakes_for_frames() const noexcept override { return false; }

    void open(const std::shared_ptr<FrameSourceHost>& host) override {
        host_ = host;
        host->set_error(platform::mikros::unavailable_message("video playback"));
    }

    void play() override { platform::mikros::unavailable("video playback"); }
    void pause() override {}
    void seek(double) override {}

    void set_volume(float volume) override {
        if (const auto host = host_.lock()) {
            host->set_volume(volume);
        }
    }

    void set_muted(bool muted) override {
        if (const auto host = host_.lock()) {
            host->set_muted(muted);
        }
    }

    void set_looping(bool looping) override {
        if (const auto host = host_.lock()) {
            host->set_looping(looping);
        }
    }

    bool update() override { return false; }
    [[nodiscard]] std::optional<VideoFrame> current() const override { return std::nullopt; }

private:
    std::weak_ptr<FrameSourceHost> host_;
};

} // namespace

std::expected<std::unique_ptr<VideoEngine>, std::string> create_video_engine(const std::string&) {
    return std::make_unique<UnavailableVideoEngine>();
}

} // namespace elg::detail
