// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Compiled with ARC.

#include "platform/macos/surface_publisher.h"

#include "core/log.h"
#include "gpu/device.h"

#include <elg/gpu_image.h>

#import <Foundation/Foundation.h>
#import <IOSurface/IOSurfaceRef.h>
#import <Metal/Metal.h>

#include <atomic>

namespace elg::detail::macos {

namespace {

std::atomic<std::uint64_t> g_next_token{1};

// Listens for the renderer's GPU reads to finish, on a queue of its own.
MTLSharedEventListener* listener() {
    static MTLSharedEventListener* shared =
        [[MTLSharedEventListener alloc] initWithDispatchQueue:dispatch_queue_create("elegance.video.release", DISPATCH_QUEUE_SERIAL)];
    return shared;
}

// The renderer is done submitting reads of the buffer in `userdata`. It's released once the
// returned fences (the GPU's reads) signal. Must not block.
void released(void* userdata, const ElgGpuImageRelease* release) {
    auto buffer = static_cast<CVPixelBufferRef>(userdata);
    if (release->status != ELG_GPU_IMAGE_OK) {
        // Whether the GPU is done is unknown: keep the buffer out of the decoder's pool for a
        // while rather than let it be overwritten under a read.
        log_warning("video: a frame's release failed (status {}); holding it a moment", release->status);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC), dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
          CVPixelBufferRelease(buffer);
        });
        return;
    }
    // Wait for every fence: chain the release through each in turn.
    __block std::size_t remaining = 0;
    for (std::size_t i = 0; i < release->fence_count; ++i) {
        if (release->fences[i].type == ELG_GPU_IMAGE_FENCE_MTL_SHARED_EVENT && release->fences[i].handle) {
            ++remaining;
        }
    }
    if (remaining == 0) {
        CVPixelBufferRelease(buffer);
        return;
    }
    auto* counter = new std::atomic<std::size_t>(remaining);
    for (std::size_t i = 0; i < release->fence_count; ++i) {
        const ElgGpuImageFence& fence = release->fences[i];
        if (fence.type != ELG_GPU_IMAGE_FENCE_MTL_SHARED_EVENT || !fence.handle) {
            continue;
        }
        // Borrowed during the callback: the block retains it.
        id<MTLSharedEvent> event = (__bridge id<MTLSharedEvent>)fence.handle;
        [event notifyListener:listener()
                      atValue:fence.value
                        block:^(id<MTLSharedEvent>, uint64_t) {
                          if (counter->fetch_sub(1) == 1) {
                              delete counter;
                              CVPixelBufferRelease(buffer);
                          }
                        }];
    }
}

} // namespace

bool SurfacePublisher::available() {
    const auto renderer = gpu::render_device();
    if (!renderer) {
        return false;
    }
    std::lock_guard lock(renderer->mutex);
    return renderer->active && renderer->backend == wgpu::BackendType::Metal &&
           renderer->device.HasFeature(wgpu::FeatureName::SharedTextureMemoryIOSurface) &&
           renderer->device.HasFeature(wgpu::FeatureName::SharedFenceMTLSharedEvent);
}

CFDictionaryRef SurfacePublisher::buffer_attributes() {
    static NSDictionary* attributes = @{
        (__bridge NSString*)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA),
        (__bridge NSString*)kCVPixelBufferIOSurfacePropertiesKey : @{},
        (__bridge NSString*)kCVPixelBufferMetalCompatibilityKey : @YES,
    };
    return (__bridge CFDictionaryRef)attributes;
}

SurfacePublisher::SurfacePublisher() : image_(elg_gpu_image_create(1, 1, ELG_GPU_IMAGE_TOP_LEFT)) {
    if (image_) {
        source_ = gpu_image_source(image_);
    }
}

SurfacePublisher::~SurfacePublisher() {
    if (image_) {
        elg_gpu_image_clear(image_);
        elg_gpu_image_destroy(image_);
    }
}

bool SurfacePublisher::publish(CVPixelBufferRef buffer) {
    IOSurfaceRef surface = CVPixelBufferGetIOSurface(buffer);
    if (!image_ || !surface || CVPixelBufferGetPixelFormatType(buffer) != kCVPixelFormatType_32BGRA) {
        return false;
    }
    ElgGpuImageFrame frame{};
    frame.transport = ELG_GPU_IMAGE_IOSURFACE;
    frame.format = ELG_GPU_IMAGE_BGRA8;
    frame.width = static_cast<std::uint32_t>(IOSurfaceGetWidth(surface));
    frame.height = static_cast<std::uint32_t>(IOSurfaceGetHeight(surface));
    frame.handle = surface;
    frame.memory_fd = -1;
    // The decoder has finished writing a buffer by the time it hands it out.
    frame.producer_fence.type = ELG_GPU_IMAGE_FENCE_NONE;
    frame.producer_fence.fd = -1;
    frame.token = g_next_token.fetch_add(1);
    frame.userdata = CVPixelBufferRetain(buffer);
    frame.release = &released;
    const std::uint32_t status = elg_gpu_image_publish(image_, &frame);
    if (status != ELG_GPU_IMAGE_OK) {
        CVPixelBufferRelease(buffer); // no callback follows a failed publish
        return false;
    }
    return true;
}

bool SurfacePublisher::update() {
    return source_ && source_->update();
}

std::optional<VideoFrame> SurfacePublisher::current() const {
    return source_ ? source_->current() : std::nullopt;
}

} // namespace elg::detail::macos
