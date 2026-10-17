// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Video sources on macOS (design.md §13.3): AVFoundation. AVPlayer decodes, plays the sound and
// keeps it in sync; an AVPlayerItemVideoOutput hands the render thread the frame that's due as
// NV12, which it packs into a `cpu` VideoFrame. Compiled with ARC.

#include "media/video_source.h"

#include "core/log.h"
#include "core/ui_tasks.h"

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

std::atomic<std::uint64_t> g_next_frame{1};

// Objective-C's `id`: inside VideoSource subclasses that name is the source's own id member.
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

} // namespace

class AVFoundationSource final : public VideoSource {
public:
    explicit AVFoundationSource(std::string url) : url_(std::move(url)) {}

    ~AVFoundationSource() override {
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

    // UI thread, right after construction (callbacks need shared_from_this()).
    void open() {
        NSURL* url = url_of(url_);
        if (!url) {
            fail(std::format("can't play {}: not a valid file path or URL", url_));
            return;
        }
        queue_ = dispatch_queue_create("elegence.video", DISPATCH_QUEUE_SERIAL);
        NSDictionary* attributes = @{
            (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        };
        output_ = [[AVPlayerItemVideoOutput alloc] initWithPixelBufferAttributes:attributes];
        item_ = [AVPlayerItem playerItemWithURL:url];
        [item_ addOutput:output_];
        player_ = [AVPlayer playerWithPlayerItem:item_];
        player_.actionAtItemEnd = AVPlayerActionAtItemEndPause;

        const std::weak_ptr<AVFoundationSource> weak =
            std::static_pointer_cast<AVFoundationSource>(shared_from_this());
        observer_ = [ElgVideoObserver new];
        observer_.onChange = ^(NSString* key_path) {
            if (const auto source = weak.lock()) {
                source->on_change(key_path);
            }
        };
        [item_ addObserver:observer_ forKeyPath:@"status" options:NSKeyValueObservingOptionInitial context:nullptr];
        [player_ addObserver:observer_
                  forKeyPath:@"timeControlStatus"
                     options:NSKeyValueObservingOptionInitial
                     context:nullptr];

        time_token_ = [player_ addPeriodicTimeObserverForInterval:CMTimeMake(1, 4)
                                                            queue:queue_
                                                       usingBlock:^(CMTime time) {
                                                         const double now = seconds_of(time);
                                                         post(weak, [now](AVFoundationSource& source) {
                                                             source.time = now;
                                                         });
                                                       }];

        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        NSOperationQueue* operations = [NSOperationQueue new];
        operations.underlyingQueue = queue_;
        notification_tokens_.push_back([center addObserverForName:AVPlayerItemDidPlayToEndTimeNotification
                                                           object:item_
                                                            queue:operations
                                                       usingBlock:^(NSNotification*) {
                                                         post(weak, [](AVFoundationSource& source) { source.on_end(); });
                                                       }]);
        notification_tokens_.push_back([center addObserverForName:AVPlayerItemFailedToPlayToEndTimeNotification
                                                           object:item_
                                                            queue:operations
                                                       usingBlock:^(NSNotification* note) {
                                                         NSError* error = note.userInfo[AVPlayerItemFailedToPlayToEndTimeErrorKey];
                                                         const std::string reason =
                                                             error ? error.localizedDescription.UTF8String : "playback stopped";
                                                         if (const auto source = weak.lock()) {
                                                             source->fail(std::format("can't play {}: {}", source->url_, reason));
                                                         }
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
        playing = true;
        playing_now_ = true;
    }

    void pause() override {
        if (player_) {
            [player_ pause];
        }
        playing = false;
        playing_now_ = false;
    }

    void seek(double seconds) override {
        seconds = std::max(0.0, seconds);
        if (player_) {
            [player_ seekToTime:CMTimeMakeWithSeconds(seconds, 600) toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
        }
        time = seconds;
    }

    void set_volume(float value) override {
        value = std::clamp(value, 0.f, 1.f);
        if (player_) {
            player_.volume = value;
        }
        volume = value;
    }

    void set_muted(bool value) override {
        if (player_) {
            player_.muted = value;
        }
        muted = value;
    }

    void set_looping(bool value) override { looping = value; }

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
        std::optional<VideoFrame> frame = pack(buffer);
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
    template <typename Action>
    static void post(const std::weak_ptr<AVFoundationSource>& weak, Action action) {
        post_ui_task([weak, action = std::move(action)] {
            if (const auto source = weak.lock()) {
                action(*source);
            }
        });
    }

    // Any thread: the error the view shows.
    void fail(std::string message) {
        playing_now_ = false;
        log_warning("video: {}", message);
        post(std::static_pointer_cast<AVFoundationSource>(shared_from_this()),
             [message = std::move(message)](AVFoundationSource& source) {
                 source.error = message;
                 source.playing = false;
             });
    }

    // KVO, on whichever thread AVFoundation changed the value.
    void on_change(NSString* key_path) {
        const auto weak = std::weak_ptr<AVFoundationSource>(std::static_pointer_cast<AVFoundationSource>(shared_from_this()));
        if ([key_path isEqualToString:@"status"]) {
            AVPlayerItem* item = item_;
            if (item.status == AVPlayerItemStatusReadyToPlay) {
                const double length = seconds_of(item.duration);
                const CGSize natural = item.presentationSize;
                post(weak, [length, natural](AVFoundationSource& source) {
                    source.duration = length;
                    source.size = Size{static_cast<float>(natural.width), static_cast<float>(natural.height)};
                    source.ready = true;
                });
            } else if (item.status == AVPlayerItemStatusFailed) {
                const std::string reason =
                    item.error ? item.error.localizedDescription.UTF8String : "the format isn't supported, or the file is missing";
                fail(std::format("can't play {}: {}", url_, reason));
            }
        } else if ([key_path isEqualToString:@"timeControlStatus"]) {
            const bool now_playing = player_.timeControlStatus != AVPlayerTimeControlStatusPaused;
            playing_now_ = now_playing;
            const double at = seconds_of(player_.currentTime);
            post(weak, [now_playing, at](AVFoundationSource& source) {
                source.playing = now_playing;
                source.time = at;
            });
        }
    }

    // UI thread: the item played to its end.
    void on_end() {
        if (looping.peek()) {
            [player_ seekToTime:kCMTimeZero];
            [player_ play];
            return;
        }
        playing = false;
        playing_now_ = false;
        time = duration.peek();
    }

    // Copies an NV12 pixel buffer into a packed frame (no row padding).
    std::optional<VideoFrame> pack(CVPixelBufferRef buffer) const {
        const OSType format = CVPixelBufferGetPixelFormatType(buffer);
        const bool full_range = format == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange;
        if (!full_range && format != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
            return std::nullopt;
        }
        if (CVPixelBufferGetPlaneCount(buffer) != 2 ||
            CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
            return std::nullopt;
        }
        const auto width = static_cast<std::uint32_t>(CVPixelBufferGetWidth(buffer));
        const auto height = static_cast<std::uint32_t>(CVPixelBufferGetHeight(buffer));
        auto pixels = std::make_shared<std::vector<std::uint8_t>>(packed_size(VideoFrame::Format::nv12, width, height));
        std::uint8_t* out = pixels->data();
        const std::size_t chroma_rows = (height + 1) / 2;
        const std::size_t chroma_row_bytes = 2 * static_cast<std::size_t>((width + 1) / 2);
        const struct {
            std::size_t rows;
            std::size_t row_bytes;
        } planes[2] = {{height, width}, {chroma_rows, chroma_row_bytes}};
        for (std::size_t plane = 0; plane < 2; ++plane) {
            const auto* in = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(buffer, plane));
            const std::size_t stride = CVPixelBufferGetBytesPerRowOfPlane(buffer, plane);
            const std::size_t rows = std::min(planes[plane].rows, CVPixelBufferGetHeightOfPlane(buffer, plane));
            const std::size_t row_bytes = std::min(planes[plane].row_bytes, stride);
            for (std::size_t row = 0; row < rows; ++row) {
                std::memcpy(out + row * planes[plane].row_bytes, in + row * stride, row_bytes);
            }
            out += planes[plane].rows * planes[plane].row_bytes;
        }
        CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);

        VideoFrame frame;
        frame.kind = VideoFrame::Kind::cpu;
        frame.width = width;
        frame.height = height;
        frame.format = VideoFrame::Format::nv12;
        frame.full_range = full_range;
        frame.matrix = VideoFrame::Matrix::bt709;
        if (CFTypeRef matrix = CVBufferCopyAttachment(buffer, kCVImageBufferYCbCrMatrixKey, nullptr)) {
            if (CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_601_4)) {
                frame.matrix = VideoFrame::Matrix::bt601;
            }
            CFRelease(matrix);
        }
        frame.pixels = std::move(pixels);
        frame.source_id = VideoSource::id;
        frame.frame_id = g_next_frame.fetch_add(1);
        return frame;
    }

    const std::string url_;
    dispatch_queue_t queue_ = nil;
    AVPlayer* player_ = nil;
    AVPlayerItem* item_ = nil;
    AVPlayerItemVideoOutput* output_ = nil;
    ElgVideoObserver* observer_ = nil;
    ObjcObject time_token_ = nil;
    std::vector<ObjcObject> notification_tokens_;
    mutable std::mutex mutex_;
    std::optional<VideoFrame> current_;
};

std::shared_ptr<VideoSource> create_avfoundation_source(std::string url) {
    auto source = std::make_shared<AVFoundationSource>(std::move(url));
    source->open();
    return source;
}

} // namespace elg::detail
