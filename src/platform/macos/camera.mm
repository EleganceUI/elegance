// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Cameras on macOS (design.md §13.3, media/video_backend.h): an AVCaptureSession with a video
// data output, delivering NV12 on its own queue, packed into `cpu` frames (pixel_buffer.h). macOS
// asks the person for access the first time (the app's Info.plist needs
// NSCameraUsageDescription, which EleganceApplication.plist.in has); when it's denied, error()
// says where to allow it. Compiled with ARC.

#include "media/video_backend.h"

#include "core/log.h"
#include "core/ui_tasks.h"
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
        self.onFrame(buffer); // which lets nothing escape (ELGSPEC-0002.R10)
    }
}
@end

namespace elg::detail {

namespace {

// Objective-C's `id`.
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

class AVFoundationCamera final : public CameraEngine {
public:
    explicit AVFoundationCamera(CameraRequest options) : options_(std::move(options)) {
        queue_ = dispatch_queue_create("elegance.camera", DISPATCH_QUEUE_SERIAL);
    }
    AVFoundationCamera(const AVFoundationCamera&) = delete;
    AVFoundationCamera& operator=(const AVFoundationCamera&) = delete;
    AVFoundationCamera(AVFoundationCamera&&) = delete;
    AVFoundationCamera& operator=(AVFoundationCamera&&) = delete;

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

    // Each frame captured wakes the render thread (on_frame()): none, none.
    [[nodiscard]] bool wakes_for_frames() const noexcept override { return true; }

    void open(const std::shared_ptr<FrameSourceHost>& host) override {
        host_ = host.get();
        weak_host_ = host;
    }

    // --- UI thread ---

    void start() override {
        if (host_->playing()) {
            return;
        }
        host_->set_error(std::string());
        host_->set_playing(true);
        // Posted work and callbacks hold the source weakly: while it lives, so does the engine.
        const std::weak_ptr<FrameSourceHost> weak = weak_host_;
        AVFoundationCamera* const camera = this;
        switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
        case AVAuthorizationStatusAuthorized: start_session(); break;
        case AVAuthorizationStatusNotDetermined: {
            // macOS asks the person; the answer comes on an arbitrary queue.
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                     completionHandler:^(BOOL granted) {
                                       run_foreign_callback([&] {
                                           post_ui_task([weak, camera, granted] {
                                               const auto host = weak.lock();
                                               if (!host || !host->playing()) {
                                                   return;
                                               }
                                               if (granted) {
                                                   camera->start_session();
                                               } else {
                                                   camera->fail(denied_message());
                                               }
                                           });
                                       });
                                     }];
            break;
        }
        case AVAuthorizationStatusDenied:
        case AVAuthorizationStatusRestricted: fail(denied_message()); break;
        }
    }

    void stop() override {
        host_->set_playing(false);
        host_->set_playing_now(false);
        if (session_) {
            AVCaptureSession* session = session_;
            dispatch_async(queue_, ^{
              [session stopRunning];
            });
        }
    }

    std::optional<VideoFrame> snapshot_frame() const override {
        std::lock_guard lock(mutex_);
        return current_;
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
    void start_session() {
        if (!session_ && !build_session()) {
            return;
        }
        host_->set_playing_now(true);
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
        // The callbacks hold the source weakly: while it lives, so does the engine.
        const std::weak_ptr<FrameSourceHost> weak = weak_host_;
        AVFoundationCamera* const camera = this;
        delegate_.onFrame = ^(CVPixelBufferRef buffer) {
          run_foreign_callback([&] {
              if (const auto alive = weak.lock()) {
                  camera->on_frame(buffer);
              }
          });
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
                                                            run_foreign_callback([&] {
                                                                NSError* failure = note.userInfo[AVCaptureSessionErrorKey];
                                                                if (const auto alive = weak.lock()) {
                                                                    camera->fail(std::format(
                                                                        "the camera stopped: {}",
                                                                        failure ? text_of(failure.localizedDescription)
                                                                                : std::string("unknown error")));
                                                                }
                                                            });
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
        std::optional<VideoFrame> frame = macos::pack_pixel_buffer(buffer, host_->source_id());
        if (!frame) {
            return;
        }
        const auto frame_width = static_cast<float>(frame->width);
        const auto frame_height = static_cast<float>(frame->height);
        bool resized = false;
        {
            std::lock_guard lock(mutex_);
            resized = !current_ || current_->width != frame->width || current_->height != frame->height;
            current_ = std::move(frame);
            ++frame_number_;
        }
        host_->notify_new_frame(); // a new frame, and only then, wakes the render thread (ELGSPEC-0027.R4)
        if (resized) {
            post_ui_task([weak = weak_host_, frame_width, frame_height] {
                if (const auto host = weak.lock()) {
                    host->set_size(frame_width, frame_height);
                    host->set_ready();
                }
            });
        }
    }

    // Any thread: the error the view shows.
    void fail(std::string message) {
        host_->set_playing_now(false);
        log_warning("camera: {}", message);
        post_ui_task([weak = weak_host_, message = std::move(message)] {
            if (const auto host = weak.lock()) {
                host->set_error(message);
                host->set_playing(false);
            }
        });
    }

    const CameraRequest options_;
    FrameSourceHost* host_ = nullptr; // set by open(); outlives the engine
    std::weak_ptr<FrameSourceHost> weak_host_; // for the callbacks and what's posted to the UI thread
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

std::vector<CameraInfo> system_cameras() {
    std::vector<CameraInfo> devices;
    for (AVCaptureDevice* device in video_devices()) {
        devices.push_back({text_of(device.uniqueID), text_of(device.localizedName)});
    }
    return devices;
}

std::unique_ptr<CameraEngine> create_camera_engine(CameraRequest request) {
    return std::make_unique<AVFoundationCamera>(std::move(request));
}

} // namespace elg::detail
