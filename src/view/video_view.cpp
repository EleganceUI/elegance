// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Video views (design.md §13.3): the picture is a VideoCommand the render thread keeps fresh by
// itself; the controls are ordinary views over the player's states.

#include <elg/video.h>
#include <elg/capture.h>
#include <elg/gpu_image.h>

#include "media/video_source.h"
#include "view/view_node.h"

#include <elg/image.h>
#include <elg/shapes.h>
#include <elg/styles.h>
#include <elg/text.h>
#include <elg/theme.h>
#include <elg/views.h>

#include <cmath>
#include <format>

namespace elg {

namespace {

std::string url_of(const std::string& source) {
    if (source.find("://") != std::string::npos) {
        return source;
    }
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(std::filesystem::path(std::u8string(source.begin(), source.end())), error);
    if (error) {
        return source;
    }
    const std::u8string text = absolute.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

} // namespace

VideoPlayer::VideoPlayer(const std::string& source) : source_(detail::VideoSource::create(url_of(source))) {}

VideoPlayer::VideoPlayer(const std::filesystem::path& file) : VideoPlayer(std::string(
                                                                    reinterpret_cast<const char*>(file.u8string().c_str()))) {}

void VideoPlayer::play() const {
    source_->play();
}
void VideoPlayer::pause() const {
    source_->pause();
}
void VideoPlayer::toggle() const {
    source_->playing.peek() ? pause() : play();
}
void VideoPlayer::stop() const {
    pause();
    seek(0);
}
void VideoPlayer::seek(double seconds) const {
    source_->seek(seconds);
}
void VideoPlayer::set_volume(float volume) const {
    source_->set_volume(volume);
}
void VideoPlayer::set_muted(bool muted) const {
    source_->set_muted(muted);
}
void VideoPlayer::set_looping(bool looping) const {
    source_->set_looping(looping);
}
bool VideoPlayer::is_playing() const {
    return source_->playing();
}
bool VideoPlayer::is_ready() const {
    return source_->ready();
}
double VideoPlayer::time() const {
    return source_->time();
}
double VideoPlayer::duration() const {
    return source_->duration();
}
Size VideoPlayer::natural_size() const {
    return source_->size();
}
float VideoPlayer::volume() const {
    return source_->volume();
}
bool VideoPlayer::is_muted() const {
    return source_->muted();
}
bool VideoPlayer::is_looping() const {
    return source_->looping();
}
std::string VideoPlayer::error() const {
    return source_->error();
}

Binding<float> VideoPlayer::progress_binding() const {
    const VideoPlayer self = *this;
    return Binding<float>(
        [self] {
            const double length = self.duration();
            return length > 0 ? static_cast<float>(std::clamp(self.time() / length, 0.0, 1.0)) : 0.f;
        },
        [self](float progress) { self.seek(static_cast<double>(progress) * self.source_->duration.peek()); });
}

Binding<float> VideoPlayer::volume_binding() const {
    const VideoPlayer self = *this;
    return Binding<float>([self] { return self.volume(); }, [self](float v) { self.set_volume(v); });
}

} // namespace elg

namespace elg::detail {

namespace {

constexpr Size kPlaceholder{320, 180}; // before the video's own size is known

class VideoElement final : public Element {
public:
    VideoElement(std::shared_ptr<VideoSource> source, ContentMode mode, bool mirrored = false, bool flipped = false)
        : source_(std::move(source)), mode_(mode), mirrored_(mirrored), flipped_(flipped) {}

    void set_size(Size size) {
        if (size.width != size_.width || size.height != size_.height) {
            size_ = size;
            invalidate_layout();
        }
    }

    void accessibility(AccessibilityInfo& out) const override { out.role = AccessRole::video; }

    void inspect(Inspection& out) const override {
        out.name = "VideoView";
        out.detail = std::format("{} × {}", size_.width, size_.height);
        out.properties.emplace_back("playing", source_->is_playing_now() ? "yes" : "no");
        out.properties.emplace_back("mode", mode_ == ContentMode::fit ? "fit" : "fill");
    }

protected:
    Size measure(ProposedSize proposal) override {
        const Size natural = size_.width > 0 && size_.height > 0 ? size_ : kPlaceholder;
        return proposal.replacing_unspecified(natural);
    }
    void arrange(Rect, ProposedSize) override {}

    void paint_impl(DisplayList& list) const override {
        const Rect bounds = frame();
        if (bounds.width <= 0 || bounds.height <= 0) {
            return;
        }
        list.commands.push_back(RectCommand{bounds, {}, ShapeStyle(Color::black), Color::clear, 0});
        // The render thread fits the exact frame it samples; size_ is only for layout.
        // A fill overflows and is clipped to the view.
        const bool clip = mode_ == ContentMode::fill;
        if (clip) {
            list.commands.push_back(PushClipCommand{bounds, {}});
        }
        // Mirrored: the texture read right to left; flipped: bottom to top.
        const Rect uv{mirrored_ ? 1.f : 0.f, flipped_ ? 1.f : 0.f, mirrored_ ? -1.f : 1.f, flipped_ ? -1.f : 1.f};
        list.commands.push_back(VideoCommand{bounds, uv, source_, 1, mode_});
        if (clip) {
            list.commands.push_back(PopClipCommand{});
        }
    }
    // Opaque over its whole frame (black behind the video), so hit there like a raster image:
    // drags and scrolls on the view reach its handlers.
    bool hit_impl(Point point, std::vector<Element*>&) override {
        const Rect r = frame();
        return point.x >= r.x && point.x < r.max_x() && point.y >= r.y && point.y < r.max_y();
    }

private:
    std::shared_ptr<VideoSource> source_;
    ContentMode mode_;
    bool mirrored_;
    bool flipped_;
    Size size_{};
};

class VideoNode final : public ViewNode {
public:
    VideoNode(std::shared_ptr<VideoSource> source, ContentMode mode, bool mirrored = false, bool flipped = false)
        : source_(std::move(source)), mode_(mode), mirrored_(mirrored), flipped_(flipped) {}

    std::unique_ptr<Element> lower(const LowerContext& context) const override {
        auto element = std::make_unique<VideoElement>(source_, mode_, mirrored_, flipped_);
        VideoElement* const target = element.get();
        // The size arrives with the video's metadata.
        effect([target, source = source_, needs_frame = context.needs_frame] {
            target->set_size(source->size());
            if (needs_frame) {
                *needs_frame = true;
            }
        });
        return element;
    }

private:
    std::shared_ptr<VideoSource> source_;
    ContentMode mode_;
    bool mirrored_;
    bool flipped_;
};

std::string clock(double seconds) {
    const int s = std::isfinite(seconds) ? static_cast<int>(seconds) : 0;
    return s >= 3600 ? std::format("{}:{:02}:{:02}", s / 3600, s / 60 % 60, s % 60) : std::format("{}:{:02}", s / 60, s % 60);
}

// Two icons with one showing: a static Image can't change, its opacity can.
View SwitchIcon(const char* on, const char* off, std::function<bool()> showing_on) {
    return ZStack(Image(on).opacity([showing_on] { return showing_on() ? 1.f : 0.f; }),
                  Image(off).opacity([showing_on] { return showing_on() ? 0.f : 1.f; }));
}

} // namespace

} // namespace elg::detail

namespace elg {

View VideoView(VideoPlayer player, ContentMode mode) {
    return detail::ViewAccess::make(std::make_shared<const detail::VideoNode>(player.source(), mode));
}

View CameraView(Camera camera, ContentMode mode) {
    return detail::ViewAccess::make(std::make_shared<const detail::VideoNode>(camera.source(), mode, camera.mirrored()));
}

View VideoFeedView(VideoFeed feed, ContentMode mode, bool mirrored) {
    return detail::ViewAccess::make(std::make_shared<const detail::VideoNode>(feed.source(), mode, mirrored));
}

View GpuImageView(const ElgGpuImage* image, ContentMode mode) {
    const auto source = gpu_image_source(image);
    if (!source) return View{};
    return detail::ViewAccess::make(std::make_shared<const detail::VideoNode>(
        source, mode, false, gpu_image_flipped(image)));
}

View VideoControls(VideoPlayer player) {
    const View play = Button(detail::SwitchIcon("pause.fill", "play.fill", [player] { return player.is_playing(); })
                                 .frame(18, 18),
                             [player] { player.toggle(); })
                          .button_style(PlainButtonStyle{})
                          .accessibility_label([player] { return std::string(player.is_playing() ? "Pause" : "Play"); });
    const View mute = Button(detail::SwitchIcon("speaker-slash.fill", "speaker-high.fill", [player] { return player.is_muted(); })
                                 .frame(18, 18),
                             [player] { player.set_muted(!player.is_muted()); })
                          .button_style(PlainButtonStyle{})
                          .accessibility_label([player] { return std::string(player.is_muted() ? "Unmute" : "Mute"); });
    const View time = Text([player] {
                          const std::string failure = player.error();
                          return failure.empty() ? detail::clock(player.time()) + " / " + detail::clock(player.duration())
                                                 : std::string("Can't play this video");
                      })
                          .font(Font::system(12))
                          .fixed_size();
    return HStack({.spacing = 12}, play,
                  Slider(player.progress_binding())
                      .accessibility_label("Position")
                      .accessibility_value([player] { return detail::clock(player.time()) + " of " + detail::clock(player.duration()); }),
                  time, mute)
        .foreground_style(Color::white)
        .padding(EdgeInsets{.top = 8, .leading = 14, .bottom = 8, .trailing = 14})
        .background(Capsule().fill(Color::black.opacity(0.55f)));
}

} // namespace elg
