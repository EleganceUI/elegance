// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Videos on macOS (design.md §13.3, media/video_backend.h): AVFoundation. AVPlayer decodes, plays
// the sound and keeps it in sync; an AVPlayerItemVideoOutput hands the render thread the frame
// that's due as an IOSurface-backed buffer the renderer imports as is (a `native` VideoFrame), or,
// when it can't, as NV12 it copies into a `cpu` VideoFrame (pixel_buffer.h). Compiled with ARC.

#include "media/video_backend.h"

#include "core/log.h"
#include "core/ui_tasks.h"
#include "platform/macos/pixel_buffer.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>
#include <mutex>

// Key-value observer for the player and its item; forwards each change to `onChange`.
@interface ElgVideoObserver : NSObject
@property(nonatomic, copy) void (^onChange)(NSString* keyPath);
@end

@implementation ElgVideoObserver
- (void)observeValueForKeyPath:(NSString*)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id>*)change
                       context:(void*)context {
    (void)object;
    (void)change;
    (void)context;
    if (self.onChange) {
        self.onChange(keyPath);
    }
}
@end

namespace elg::detail {

namespace {

// Objective-C's `id`.
using ObjcObject = id;

double seconds_of(CMTime time) {
    const double seconds = CMTimeGetSeconds(time);
    return std::isfinite(seconds) ? seconds : 0.0;
}

NSURL* url_of(const std::string& url) {
    NSString* text = [NSString stringWithUTF8String:url.c_str()];
    if (!text) {
        return nil;
    }
    if (url.find("://") != std::string::npos) {
        return [NSURL URLWithString:text];
    }
    return [NSURL fileURLWithPath:text];
}

// The pixel buffers to ask the decoder for when the renderer imports them as they are: BGRA,
// IOSurface-backed, Metal-compatible.
NSDictionary* native_buffer_attributes() {
    static NSDictionary* attributes = @{
        (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
        (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey : @{},
        (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey : @YES,
    };
    return attributes;
}

// A frame of the decoder's `buffer` as it is, for the renderer to import: it retains `buffer`.
VideoFrame native_frame(CVPixelBufferRef buffer, std::uint64_t source_id) {
    VideoFrame frame;
    frame.kind = VideoFrame::Kind::native;
    frame.width = static_cast<std::uint32_t>(CVPixelBufferGetWidth(buffer));
    frame.height = static_cast<std::uint32_t>(CVPixelBufferGetHeight(buffer));
    frame.format = VideoFrame::Format::bgra;
    frame.source_id = source_id;
    frame.native = std::shared_ptr<void>(CVPixelBufferRetain(buffer), [](void* retained) {
        CVPixelBufferRelease(static_cast<CVPixelBufferRef>(retained));
    });
    return frame;
}

} // namespace

class AVFoundationVideo final : public VideoEngine {
public:
    explicit AVFoundationVideo(std::string url) : url_(std::move(url)) {}
    AVFoundationVideo(const AVFoundationVideo&) = delete;
    AVFoundationVideo& operator=(const AVFoundationVideo&) = delete;
    AVFoundationVideo(AVFoundationVideo&&) = delete;
    AVFoundationVideo& operator=(AVFoundationVideo&&) = delete;

    ~AVFoundationVideo() override {
        if (player_) {
            [player_ pause];
            if (time_token_) {
                [player_ removeTimeObserver:time_token_];
            }
            [player_ removeObserver:observer_ forKeyPath:@"timeControlStatus"];
        }
        if (item_) {
            [item_ removeObserver:observer_ forKeyPath:@"status"];
        }
        for (ObjcObject token : notification_tokens_) {
            [[NSNotificationCenter defaultCenter] removeObserver:token];
        }
    }

    // The render thread looks for frames while it plays.
    [[nodiscard]] bool wakes_for_frames() const noexcept override { return false; }

    void open(const std::shared_ptr<FrameSourceHost>& host) override {
        host_ = host.get();
        weak_host_ = host;
        NSURL* url = url_of(url_);
        if (!url) {
            fail(std::format("can't play {}: not a valid file path or URL", url_));
            return;
        }
        queue_ = dispatch_queue_create("elegance.video", DISPATCH_QUEUE_SERIAL);
        // Zero-copy when the renderer can import IOSurfaces (it's running, on Metal); else NV12,
        // copied each frame.
        NSDictionary* attributes = @{
            (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey : @(macos::kPreferredPixelFormat),
        };
        if (host_->imports_native_frames()) {
            imports_ = true;
            attributes = native_buffer_attributes();
        }
        output_ = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:attributes];
        item_ = [AVPlayerItem playerItemWithURL:url];
        [item_ addOutput:output_];
        player_ = [AVPlayer playerWithPlayerItem:item_];
        player_.actionAtItemEnd = AVPlayerActionAtItemEndPause;

        // The callbacks hold the source weakly: while it lives, so does the engine.
        const std::weak_ptr<FrameSourceHost> weak = weak_host_;
        AVFoundationVideo* const video = this;
        observer_ = [ElgVideoObserver new];
        // Called by AVFoundation: nothing may escape them (ELGSPEC-0002.R10).
        observer_.onChange = ^(NSString* key_path) {
          run_foreign_callback([&] {
              if (const auto alive = weak.lock()) {
                  video->on_change(key_path);
              }
          });
        };
        [item_ addObserver:observer_ forKeyPath:@"status" options:NSKeyValueObservingOptionInitial context:nullptr];
        [player_ addObserver:observer_
                  forKeyPath:@"timeControlStatus"
                     options:NSKeyValueObservingOptionInitial
                     context:nullptr];

        time_token_ = [player_ addPeriodicTimeObserverForInterval:CMTimeMake(1, 4)
                                                            queue:queue_
                                                       usingBlock:^(CMTime time) {
                                                         run_foreign_callback([&] {
                                                             const double now = seconds_of(time);
                                                             post(weak, [now](FrameSourceHost& source) {
                                                                 source.set_time(now);
                                                             });
                                                         });
                                                       }];

        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        NSOperationQueue* operations = [NSOperationQueue new];
        operations.underlyingQueue = queue_;
        notification_tokens_.push_back([center addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                                                           object:item_
                                                            queue:operations
                                                       usingBlock:^(NSNotification*) {
                                                         run_foreign_callback([&] {
                                                             post(weak, [video](FrameSourceHost& source) {
                                                                 video->on_end(source);
                                                             });
                                                         });
                                                       }]);
        notification_tokens_.push_back([center addObserverForName:AVPlayerItemFailedToPlayToEndTimeNotification
                                                           object:item_
                                                            queue:operations
                                                       usingBlock:^(NSNotification* note) {
                                                         run_foreign_callback([&] {
                                                             NSError* error =
                                                                 note.userInfo[AVPlayerItemFailedToPlayToEndTimeErrorKey];
                                                             const std::string reason = error
                                                                                            ? error.localizedDescription.UTF8String
                                                                                            : "playback stopped";
                                                             if (const auto alive = weak.lock()) {
                                                                 video->fail(
                                                                     std::format("can't play {}: {}", video->url_, reason));
                                                             }
                                                         });
                                                       }]);
    }

    // --- UI thread ---

    void play() override {
        if (!player_) {
            return;
        }
        if (item_ && CMTimeCompare(item_.currentTime, item_.duration) >= 0 && CMTIME_IS_NUMERIC(item_.duration)) {
            [player_ seekToTime:kCMTimeZero];
        }
        [player_ play];
        host_->set_playing(true);
        host_->set_playing_now(true);
    }

    void pause() override {
        if (player_) {
            [player_ pause];
        }
        host_->set_playing(false);
        host_->set_playing_now(false);
    }

    void seek(double seconds) override {
        seconds = std::max(0.0, seconds);
        if (player_) {
            [player_ seekToTime:CMTimeMakeWithSeconds(seconds, 600) toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
        }
        host_->set_time(seconds);
    }

    void set_volume(float value) override {
        value = std::clamp(value, 0.f, 1.f);
        if (player_) {
            player_.volume = value;
        }
        host_->set_volume(value);
    }

    void set_muted(bool value) override {
        if (player_) {
            player_.muted = value;
        }
        host_->set_muted(value);
    }

    void set_looping(bool value) override { host_->set_looping(value); }

    // --- Render thread ---

    bool update() override {
        AVPlayerItemVideoOutput* output = output_;
        if (!output) {
            return false;
        }
        const CMTime now = [output itemTimeForHostTime:CACurrentMediaTime()];
        if (![output hasNewPixelBufferForItemTime:now]) {
            return false;
        }
        CVPixelBufferRef buffer = [output copyPixelBufferForItemTime:now itemTimeForDisplay:nullptr];
        if (!buffer) {
            return false;
        }
        if (imports_ && !copies_) {
            std::optional<VideoFrame> imported =
                host_->import_native_frame(native_frame(buffer, host_->source_id()));
            if (imported) {
                CVPixelBufferRelease(buffer);
                std::lock_guard lock(mutex_);
                current_ = std::move(imported);
                return true;
            }
            // The device went away or refused it: copy from now on.
            log_warning("video: can't share frames with the renderer; copying them instead");
            copies_ = true;
            std::lock_guard lock(mutex_);
            current_.reset(); // until the first copy
        }
        std::optional<VideoFrame> frame = macos::pack_pixel_buffer(buffer, host_->source_id());
        CVPixelBufferRelease(buffer);
        if (!frame) {
            return false;
        }
        std::lock_guard lock(mutex_);
        current_ = std::move(frame);
        return true;
    }

    std::optional<VideoFrame> current() const override {
        std::lock_guard lock(mutex_);
        return current_;
    }

private:
    // Runs `action` with the host on the UI thread, unless the source is gone by then (the engine
    // lives as long as its source).
    template <typename Action>
    static void post(const std::weak_ptr<FrameSourceHost>& weak, Action action) {
        post_ui_task([weak, action = std::move(action)] {
            if (const auto host = weak.lock()) {
                action(*host);
            }
        });
    }

    // Any thread: the error the view shows.
    void fail(std::string message) {
        host_->set_playing_now(false);
        log_warning("video: {}", message);
        post(weak_host_, [message = std::move(message)](FrameSourceHost& source) {
            source.set_error(message);
            source.set_playing(false);
        });
    }

    // KVO, on whichever thread AVFoundation changed the value.
    void on_change(NSString* key_path) {
        const std::weak_ptr<FrameSourceHost>& weak = weak_host_;
        if ([key_path isEqualToString:@"status"]) {
            AVPlayerItem* item = item_;
            if (item.status == AVPlayerItemStatusReadyToPlay) {
                const double length = seconds_of(item.duration);
                const CGSize natural = item.presentationSize;
                post(weak, [length, natural](FrameSourceHost& source) {
                    source.set_duration(length);
                    source.set_size(static_cast<float>(natural.width), static_cast<float>(natural.height));
                    source.set_ready();
                });
            } else if (item.status == AVPlayerItemStatusFailed) {
                const std::string reason =
                    item.error ? item.error.localizedDescription.UTF8String : "the format isn't supported, or the file is missing";
                fail(std::format("can't play {}: {}", url_, reason));
            }
        } else if ([key_path isEqualToString:@"timeControlStatus"]) {
            const bool now_playing = player_.timeControlStatus != AVPlayerTimeControlStatusPaused;
            host_->set_playing_now(now_playing);
            const double at = seconds_of(player_.currentTime);
            post(weak, [now_playing, at](FrameSourceHost& source) {
                source.set_playing(now_playing);
                source.set_time(at);
            });
        }
    }

    // UI thread: the item played to its end.
    void on_end(FrameSourceHost& host) {
        if (host.looping()) {
            [player_ seekToTime:kCMTimeZero];
            [player_ play];
            return;
        }
        host.set_playing(false);
        host.set_playing_now(false);
        host.set_time(host.duration());
    }

    const std::string url_;
    FrameSourceHost* host_ = nullptr; // set by open(); outlives the engine
    std::weak_ptr<FrameSourceHost> weak_host_; // for the callbacks and what's posted to the UI thread
    dispatch_queue_t queue_ = nil;
    AVPlayer* player_ = nil;
    AVPlayerItem* item_ = nil;
    AVPlayerItemVideoOutput* output_ = nil;
    ElgVideoObserver* observer_ = nil;
    ObjcObject time_token_ = nil;
    std::vector<ObjcObject> notification_tokens_;
    // Zero-copy frames: the renderer imports them (set by open()). copies_: an import failed, and
    // frames are copied from then on (render thread).
    bool imports_ = false;
    bool copies_ = false;
    mutable std::mutex mutex_;
    std::optional<VideoFrame> current_;
};

std::expected<std::unique_ptr<VideoEngine>, std::string> create_video_engine(const std::string& url) {
    return std::make_unique<AVFoundationVideo>(url);
}

} // namespace elg::detail
