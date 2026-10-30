// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Cameras on macOS (design.md §13.3): an AVCaptureSession with a video data output, delivering
// NV12 on its own queue, packed into `cpu` frames (pixel_buffer.h). macOS asks the person for
// access the first time (the app's Info.plist needs NSCameraUsageDescription, which
// EleganceApplication.plist.in has); when it's denied, error() says where to allow it. Compiled
// with ARC.

#include "media/camera_source.h"

#include "core/log.h"
#include "core/ui_tasks.h"
#include "media/image_decode.h"
#include "platform/macos/pixel_buffer.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

#include <cmath>
#include <format>
#include <limits>
#include <mutex>

// Forwards each frame from the capture queue to `onFrame`.
@interface ElgCameraDelegate : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate>
@property(nonatomic, copy) void (^onFrame)(CVPixelBufferRef buffer);
@end

@implementation ElgCameraDelegate
- (void)captureOutput:(AVCaptureOutput*)output
    didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
           fromConnection:(AVCaptureConnection*)connection {
    (void)output;
    (void)connection;
    if (CVPixelBufferRef buffer = CMSampleBufferGetImageBuffer(sampleBuffer); buffer && self.onFrame) {
        self.onFrame(buffer);
    }
}
@end

namespace elg::detail {

namespace {

// Objective-C's `id`: inside VideoSource subclasses that name is the source's own id member.
using ObjcObject = id;

NSArray<AVCaptureDevice*>* video_devices() {
    NSMutableArray<AVCaptureDeviceType>* types = [NSMutableArray arrayWithObject:AVCaptureDeviceTypeBuiltInWideAngleCamera];
    if (@available(macOS 14.0, *)) {
        [types addObject:AVCaptureDeviceTypeExternal];
        [types addObject:AVCaptureDeviceTypeContinuityCamera];
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        [types addObject:AVCaptureDeviceTypeExternalUnknown];
#pragma clang diagnostic pop
    }
    return [AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:types
                                                                  mediaType:AVMediaTypeVideo
                                                                   position:AVCaptureDevicePositionUnspecified]
        .devices;
}

std::string text_of(NSString* text) {
    return text ? std::string(text.UTF8String) : std::string();
}

class AVFoundationCamera final : public CameraSource {
public:
    explicit AVFoundationCamera(CameraOptions options) : options_(std::move(options)) {
        queue_ = dispatch_queue_create("elegance.camera", DISPATCH_QUEUE_SERIAL);
    }

    ~AVFoundationCamera() override {
        if (session_) {
            AVCaptureSession* session = session_;
            dispatch_sync(queue_, ^{
              [session stopRunning];
            });
        }
        if (runtime_error_token_) {
            [[NSNotificationCenter defaultCenter] removeObserver:runtime_error_token_];
        }
        delegate_.onFrame = nil;
    }

    // --- UI thread ---

    void play() override {
        if (playing.peek()) {
            return;
        }
        error = std::string();
        playing = true;
        const std::weak_ptr<AVFoundationCamera> weak = std::static_pointer_cast<AVFoundationCamera>(shared_from_this());
        switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
        case AVAuthorizationStatusAuthorized: start(); break;
        case AVAuthorizationStatusNotDetermined: {
            // macOS asks the person; the answer comes on an arbitrary queue.
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                     completionHandler:^(BOOL granted) {
                                       post_ui_task([weak, granted] {
                                           const auto camera = weak.lock();
                                           if (!camera || !camera->playing.peek()) {
                                               return;
                                           }
                                           if (granted) {
                                               camera->start();
                                           } else {
                                               camera->fail(denied_message());
                                           }
                                       });
                                     }];
            break;
        }
        case AVAuthorizationStatusDenied:
        case AVAuthorizationStatusRestricted: fail(denied_message()); break;
        }
    }

    void pause() override {
        playing = false;
        playing_now_ = false;
        if (session_) {
            AVCaptureSession* session = session_;
            dispatch_async(queue_, ^{
              [session stopRunning];
            });
        }
    }

    void seek(double) override {}
    void set_volume(float) override {}
    void set_muted(bool) override {}
    void set_looping(bool) override {}

    std::optional<std::vector<unsigned char>> snapshot() const override {
        std::optional<VideoFrame> frame;
        {
            std::lock_guard lock(mutex_);
            frame = current_;
        }
        if (!frame || !frame->pixels) {
            return std::nullopt;
        }
        const std::vector<std::uint8_t> bgra =
            frame_to_bgra(frame->format, frame->matrix, frame->full_range, *frame->pixels, frame->width, frame->height);
        if (bgra.empty()) {
            return std::nullopt;
        }
        return encode_png(bgra, frame->width, frame->height);
    }

    // --- Render thread ---

    bool update() override {
        std::lock_guard lock(mutex_);
        if (frame_number_ == reported_) {
            return false;
        }
        reported_ = frame_number_;
        return true;
    }

    std::optional<VideoFrame> current() const override {
        std::lock_guard lock(mutex_);
        return current_;
    }

private:
    static std::string denied_message() {
        return "camera access is off for this app: allow it in System Settings > Privacy & Security > Camera";
    }

    // UI thread: builds the session the first time, then starts it on the capture queue (it blocks).
    void start() {
        if (!session_ && !build_session()) {
            return;
        }
        playing_now_ = true;
        AVCaptureSession* session = session_;
        dispatch_async(queue_, ^{
          [session startRunning];
        });
    }

    bool build_session() {
        AVCaptureDevice* device = nil;
        if (options_.device_id.empty()) {
            device = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo];
        } else {
            device = [AVCaptureDevice deviceWithUniqueID:[NSString stringWithUTF8String:options_.device_id.c_str()]];
        }
        if (!device) {
            fail(options_.device_id.empty() ? std::string("no camera was found")
                                            : std::format("the camera {} isn't connected", options_.device_id));
            return false;
        }

        NSError* error = nil;
        AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&error];
        if (!input) {
            fail(std::format("can't open the camera {}: {}", text_of(device.localizedName),
                             text_of(error.localizedDescription)));
            return false;
        }
        AVCaptureSession* session = [AVCaptureSession new];
        [session beginConfiguration];
        if (![session canAddInput:input]) {
            fail(std::format("can't capture from the camera {}", text_of(device.localizedName)));
            return false;
        }
        [session addInput:input];
        // After adding the input, which applies the session's preset: a format set on the device
        // then wins over it.
        choose_format(device);

        AVCaptureVideoDataOutput* output = [AVCaptureVideoDataOutput new];
        output.alwaysDiscardsLateVideoFrames = YES;
        output.videoSettings = @{(__bridge NSString*)kCVPixelBufferPixelFormatTypeKey : @(macos::kPreferredPixelFormat)};
        delegate_ = [ElgCameraDelegate new];
        const std::weak_ptr<AVFoundationCamera> weak = std::static_pointer_cast<AVFoundationCamera>(shared_from_this());
        delegate_.onFrame = ^(CVPixelBufferRef buffer) {
          if (const auto camera = weak.lock()) {
              camera->on_frame(buffer);
          }
        };
        [output setSampleBufferDelegate:delegate_ queue:queue_];
        if (![session canAddOutput:output]) {
            fail(std::format("can't capture from the camera {}", text_of(device.localizedName)));
            return false;
        }
        [session addOutput:output];
        [session commitConfiguration];

        runtime_error_token_ =
            [[NSNotificationCenter defaultCenter] addObserverForName:AVCaptureSessionRuntimeErrorNotification
                                                              object:session
                                                               queue:nil
                                                          usingBlock:^(NSNotification* note) {
                                                            NSError* failure = note.userInfo[AVCaptureSessionErrorKey];
                                                            if (const auto camera = weak.lock()) {
                                                                camera->fail(std::format(
                                                                    "the camera stopped: {}",
                                                                    failure ? text_of(failure.localizedDescription)
                                                                            : std::string("unknown error")));
                                                            }
                                                          }];
        session_ = session;
        log_info("camera: {} ({})", text_of(device.localizedName), text_of(device.uniqueID));
        return true;
    }

    // The device format closest to the requested size (by area, then aspect), at its highest rate.
    void choose_format(AVCaptureDevice* device) const {
        AVCaptureDeviceFormat* best = nil;
        double best_score = std::numeric_limits<double>::max();
        const double wanted_area = static_cast<double>(options_.width) * options_.height;
        const double wanted_aspect = options_.height ? static_cast<double>(options_.width) / options_.height : 16.0 / 9;
        for (AVCaptureDeviceFormat* format in device.formats) {
            const CMVideoDimensions size = CMVideoFormatDescriptionGetDimensions(format.formatDescription);
            if (size.width <= 0 || size.height <= 0) {
                continue;
            }
            const double area = static_cast<double>(size.width) * size.height;
            const double aspect = static_cast<double>(size.width) / size.height;
            const double score = std::abs(std::log(area / wanted_area)) + 2 * std::abs(std::log(aspect / wanted_aspect));
            if (score < best_score) {
                best_score = score;
                best = format;
            }
        }
        if (best && [device lockForConfiguration:nil]) {
            device.activeFormat = best;
            [device unlockForConfiguration];
        }
    }

    // Capture queue.
    void on_frame(CVPixelBufferRef buffer) {
        std::optional<VideoFrame> frame = macos::pack_pixel_buffer(buffer, VideoSource::id);
        if (!frame) {
            return;
        }
        const Size frame_size{static_cast<float>(frame->width), static_cast<float>(frame->height)};
        bool resized = false;
        {
            std::lock_guard lock(mutex_);
            resized = !current_ || current_->width != frame->width || current_->height != frame->height;
            current_ = std::move(frame);
            ++frame_number_;
        }
        if (resized) {
            const std::weak_ptr<AVFoundationCamera> weak =
                std::static_pointer_cast<AVFoundationCamera>(shared_from_this());
            post_ui_task([weak, frame_size] {
                if (const auto camera = weak.lock()) {
                    camera->size = frame_size;
                    camera->ready = true;
                }
            });
        }
    }

    // Any thread: the error the view shows.
    void fail(std::string message) {
        playing_now_ = false;
        log_warning("camera: {}", message);
        const std::weak_ptr<AVFoundationCamera> weak = std::static_pointer_cast<AVFoundationCamera>(shared_from_this());
        post_ui_task([weak, message = std::move(message)] {
            if (const auto camera = weak.lock()) {
                camera->error = message;
                camera->playing = false;
            }
        });
    }

    const CameraOptions options_;
    dispatch_queue_t queue_ = nil;
    AVCaptureSession* session_ = nil;
    ElgCameraDelegate* delegate_ = nil;
    ObjcObject runtime_error_token_ = nil;

    mutable std::mutex mutex_;
    std::optional<VideoFrame> current_;
    std::uint64_t frame_number_ = 0;
    std::uint64_t reported_ = 0;
};

} // namespace

std::vector<CameraDevice> platform_camera_devices() {
    std::vector<CameraDevice> devices;
    for (AVCaptureDevice* device in video_devices()) {
        devices.push_back({text_of(device.uniqueID), text_of(device.localizedName)});
    }
    return devices;
}

std::shared_ptr<CameraSource> create_platform_camera(CameraOptions options) {
    return std::make_shared<AVFoundationCamera>(std::move(options));
}

} // namespace elg::detail
