// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Zero-copy video frames on macOS: IOSurface-backed BGRA pixel buffers imported into the
// renderer's Metal device as they are (as elg/gpu_image.h's IOSurface transport does), rather
// than copied into memory and uploaded again. Each buffer stays retained until the renderer is
// done with it, including the GPU's reads, so the decoder's pool can't reuse it early. Compiled
// with ARC.

#include "gpu/native_image.h"

#include "core/log.h"
#include "gpu/external_image.h"
#include "gpu/render_device.h"

#include <elg/gpu_image.h>

#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurfaceRef.h>
#import <Metal/Metal.h>

#include <atomic>

namespace elg::detail::gpu {

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

bool imports_native_images() {
    const auto renderer = gpu::render_device();
    if (!renderer) {
        return false;
    }
    std::lock_guard lock(renderer->mutex);
    return renderer->active && renderer->backend == wgpu::BackendType::Metal &&
           renderer->device.HasFeature(wgpu::FeatureName::SharedTextureMemoryIOSurface) &&
           renderer->device.HasFeature(wgpu::FeatureName::SharedFenceMTLSharedEvent);
}

NativeImport import_native_image(void* image) {
    auto buffer = static_cast<CVPixelBufferRef>(image);
    IOSurfaceRef surface = CVPixelBufferGetIOSurface(buffer);
    if (!surface || CVPixelBufferGetPixelFormatType(buffer) != kCVPixelFormatType_32BGRA) {
        return {};
    }
    ElgGpuImageFrame frame{};
    frame.transport = ELG_GPU_IMAGE_IOSURFACE;
    frame.format = ELG_GPU_IMAGE_BGRA8;
    frame.width = static_cast<std::uint32_t>(IOSurfaceGetWidth(surface));
    frame.height = static_cast<std::uint32_t>(IOSurfaceGetHeight(surface));
    if (frame.width == 0 || frame.height == 0) {
        return {}; // as elg_gpu_image_publish() refuses it
    }
    frame.handle = surface;
    frame.memory_fd = -1;
    // The decoder has finished writing a buffer by the time it hands it out.
    frame.producer_fence.type = ELG_GPU_IMAGE_FENCE_NONE;
    frame.producer_fence.fd = -1;
    frame.token = g_next_token.fetch_add(1);
    frame.userdata = CVPixelBufferRetain(buffer);
    frame.release = &released;
    NativeImport imported;
    if (import_external_image(frame, imported.image) != ELG_GPU_IMAGE_OK) {
        CVPixelBufferRelease(buffer); // no callback follows a failed import
        return {};
    }
    imported.width = frame.width;
    imported.height = frame.height;
    return imported;
}

} // namespace elg::detail::gpu
