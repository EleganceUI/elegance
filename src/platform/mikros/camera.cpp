// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Cameras on Mikros (media/video_backend.h), which has none: the list is empty, the engine that a
// VideoSource gets is made and says so in error(), and starting it fails with the error of
// platform/mikros/unavailable.h.

#include "media/video_backend.h"

#include "platform/mikros/unavailable.h"

#include <memory>
#include <vector>

namespace elg::detail {

namespace {

class UnavailableCameraEngine final : public CameraEngine {
public:
    [[nodiscard]] bool wakes_for_frames() const noexcept override { return false; }

    void open(const std::shared_ptr<FrameSourceHost>& host) override {
        host->set_error(platform::mikros::unavailable_message("cameras"));
    }

    void start() override { platform::mikros::unavailable("cameras"); }
    void stop() override {}
    [[nodiscard]] std::optional<VideoFrame> snapshot_frame() const override { return std::nullopt; }

    bool update() override { return false; }
    [[nodiscard]] std::optional<VideoFrame> current() const override { return std::nullopt; }
};

} // namespace

std::vector<CameraInfo> system_cameras() {
    return {};
}

std::unique_ptr<CameraEngine> create_camera_engine(CameraRequest) {
    return std::make_unique<UnavailableCameraEngine>();
}

} // namespace elg::detail
