// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_IMAGE_H
#define ELG_GPU_IMAGE_H

#include <elg/export.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// An image a native GPU producer publishes frames into, shown by GpuImageView(). Made with
/// elg_gpu_image_create() and freed with elg_gpu_image_destroy().
typedef struct ElgGpuImage ElgGpuImage;
// Fixed-width tags, not C enums, so Rust can use repr(C) without enum validity hazards.
#define ELG_GPU_IMAGE_OK 0u                 ///< Success.
#define ELG_GPU_IMAGE_INVALID_ARGUMENT 1u   ///< A null pointer, or a field out of range or missing.
#define ELG_GPU_IMAGE_DEVICE_UNAVAILABLE 2u ///< No live renderer to import into.
#define ELG_GPU_IMAGE_UNSUPPORTED 3u        ///< The renderer's backend or device can't import this transport.
#define ELG_GPU_IMAGE_IMPORT_FAILED 4u      ///< The import failed, or the memory doesn't match the frame.
#define ELG_GPU_IMAGE_SYNC_FAILED 5u        ///< Releasing: the fences couldn't be exported or waited for.
#define ELG_GPU_IMAGE_INTERNAL_ERROR 6u     ///< An unexpected failure inside Elegance.

#define ELG_GPU_IMAGE_IOSURFACE 1u
#define ELG_GPU_IMAGE_DXGI_HANDLE 2u
#define ELG_GPU_IMAGE_VULKAN_OPAQUE_FD 3u
#define ELG_GPU_IMAGE_RGBA8 1u
#define ELG_GPU_IMAGE_BGRA8 2u
#define ELG_GPU_IMAGE_TOP_LEFT 0u
#define ELG_GPU_IMAGE_BOTTOM_LEFT 1u

#define ELG_GPU_IMAGE_FENCE_NONE 0u
#define ELG_GPU_IMAGE_FENCE_MTL_SHARED_EVENT 1u
#define ELG_GPU_IMAGE_FENCE_DXGI_HANDLE 2u
#define ELG_GPU_IMAGE_FENCE_VK_OPAQUE_FD 3u

/// A GPU fence shared between the producer and Elegance: ElgGpuImageFrame::producer_fence, which
/// the producer's writes signal, or one of ElgGpuImageRelease::fences, which Elegance's reads
/// signal.
typedef struct ElgGpuImageFence {
    uint32_t type;  ///< One of the ELG_GPU_IMAGE_FENCE_* tags; the frame's must match its transport.
    void* handle;   ///< id<MTLSharedEvent> or shared D3D fence NT HANDLE; not a WebGPU handle
    int fd;         ///< VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT, binary semaphore
    uint64_t value; ///< Metal/D3D timeline value; Vulkan binary semaphore: 1
} ElgGpuImageFence;

/// What the release callback gets once Elegance submits no more GPU work that reads a published
/// frame. The record and the fences in it are borrowed only during the callback.
typedef struct ElgGpuImageRelease {
    /// ELG_GPU_IMAGE_OK, or ELG_GPU_IMAGE_SYNC_FAILED / ELG_GPU_IMAGE_INTERNAL_ERROR: then
    /// synchronization is unknown (see elg_gpu_image_publish()).
    uint32_t status;
    uint64_t token; ///< unchanged producer token, not used as a cache identity
    /// The fences to wait for before reusing or freeing the allocation; null unless `status` is OK.
    const ElgGpuImageFence* fences;
    size_t fence_count;    ///< How many `fences` there are.
    int32_t vk_old_layout; ///< Vulkan: the old layout of the transition back to the producer.
    int32_t vk_new_layout; ///< Vulkan: the new layout of the transition back to the producer.
} ElgGpuImageRelease;

/// Called exactly once for each accepted frame, on any thread, when Elegance is done with it (see
/// elg_gpu_image_publish()). Must not throw, block waiting for renderer work, or call this API.
///
/// @param userdata ElgGpuImageFrame::userdata of the frame.
/// @param release The status, the frame's token, and the fences to wait for; borrowed only during
///   the call.
typedef void (*ElgGpuImageReleaseCallback)(void* userdata, const ElgGpuImageRelease* release);

/// One frame for elg_gpu_image_publish(): a 2D texture allocated by the producer, and how to
/// import and release it.
typedef struct ElgGpuImageFrame {
    /// ELG_GPU_IMAGE_IOSURFACE (Metal), ELG_GPU_IMAGE_DXGI_HANDLE (D3D12) or
    /// ELG_GPU_IMAGE_VULKAN_OPAQUE_FD (Vulkan); it must match the renderer's backend.
    uint32_t transport;
    uint32_t format; ///< ELG_GPU_IMAGE_RGBA8 or ELG_GPU_IMAGE_BGRA8: the texture's UNORM format.
    uint32_t width;  ///< The texture's width in pixels; not 0.
    uint32_t height; ///< The texture's height in pixels; not 0.
    void* handle;    ///< IOSurfaceRef (not IOSurfaceID), or DXGI texture NT HANDLE
    int memory_fd;   ///< Vulkan only: the allocation's opaque FD; not negative.
    /// Vulkan only: exact VkImageCreateInfo including its pNext chain, matching the exported
    /// image bound at allocation offset zero (no suballocations). Borrowed for publish() only.
    /// Same physical device/compatible memory type required.
    const void* vk_image_create_info;
    uint32_t vk_memory_type_index;    ///< Vulkan only: the allocation's memory type index.
    uint64_t vk_allocation_size;      ///< Vulkan only: the allocation's size in bytes; not 0.
    uint32_t vk_dedicated_allocation; ///< 0 or 1
    int32_t vk_old_layout; ///< Vulkan only: the old layout of the producer's ownership release.
    int32_t vk_new_layout; ///< Vulkan only: the new layout of the producer's ownership release.
    /// Signaled when the producer's writes are done; type ELG_GPU_IMAGE_FENCE_NONE asserts that
    /// they already are.
    ElgGpuImageFence producer_fence;
    uint64_t token;                     ///< The producer's own value, handed back in the release.
    void* userdata;                     ///< Passed to `release`.
    ElgGpuImageReleaseCallback release; ///< Required: called once Elegance is done with the frame.
} ElgGpuImageFrame;

/// Unsafe, low-level native sharing API. No CPU upload/readback and no conversion fallback.
/// Publication requires a live renderer. Fixed origin; one mip/layer/sample, 2D RGBA/BGRA
/// UNORM storage, sRGB-encoded RGB, straight alpha. Import must permit an sRGB sampling view.
/// Create dimensions must be nonzero and supply the initial natural size until a frame is accepted.
/// Each publication may have different nonzero dimensions; successful frames update natural size
/// asynchronously on the UI thread without replacing the source. Clear retains the last natural size.
/// No YUV, keyed mutex, DMA-BUF, cross-adapter or device-loss recovery support.
///
/// @param width The initial natural width in pixels; not 0.
/// @param height The initial natural height in pixels; not 0.
/// @param origin ELG_GPU_IMAGE_TOP_LEFT, or ELG_GPU_IMAGE_BOTTOM_LEFT for images stored bottom
///   row first (views flip them).
/// @return The image, owned by the caller (free it with elg_gpu_image_destroy()), or null when an
///   argument is invalid or allocation fails.
ELG_API ElgGpuImage* elg_gpu_image_create(uint32_t width, uint32_t height, uint32_t origin);
/// Frees the handle. Not a drain: views showing the image keep its last frame, and release
/// callbacks still come.
///
/// @param image The image, or null (nothing happens). Must not be in use by another thread.
ELG_API void elg_gpu_image_destroy(ElgGpuImage* image);
/// Thread-safe against publish/clear on a LIVE handle, not against destroy. Synchronous import;
/// returns a status above. On failure ownership stays with caller, no callback and no reads;
/// the previous frame and natural size remain unchanged.
/// On success, exactly one release callback follows, possibly before publish returns if another
/// thread clears/replaces the frame. Keep userdata/resources alive until that callback.
///
/// Each accepted publication is a fresh, exclusive allocation: never publish aliases of an
/// outstanding allocation. Stop all producer writes before publication. Supply its producer
/// fence, or NONE as an explicit assertion that producer GPU writes have ALREADY COMPLETED.
/// Handles/FDs are borrowed, never closed/released by Elegance; keep them and the allocation
/// alive until release. A Vulkan semaphore must be exportable/importable as a BINARY semaphore.
/// Producer must release external queue-family ownership as required by Dawn's Vulkan sharing
/// contract and supply the corresponding old/new layouts. Elegance returns the reverse layout
/// transition at retirement; producer must acquire ownership before accessing the image again.
///
/// Release means NO FUTURE consumer submissions, NOT GPU completion. All returned fences must
/// be imported/waited before overwriting, recycling OR freeing the allocation. Fence handles
/// and the release record are BORROWED ONLY DURING CALLBACK: duplicate/retain them there.
/// A consuming native import (e.g. vkImportSemaphoreFdKHR) must consume a DUPLICATE, not the
/// borrowed exported FD/handle.
/// On non-OK release, synchronization is unknown: quarantine the allocation until independently
/// establishing GPU completion/device teardown; do not infer safety from an empty fence list.
/// Retirement normally runs asynchronously on an internal worker; clear/destroy do not drain
/// callbacks. Metal scheduling waits are bounded (5 seconds), then report SYNC_FAILED.
/// Callback may run on any thread, must not throw/unwind, block waiting for renderer work, or
/// reenter this API. destroy is not a drain: views/snapshots may retain frames. Accepted frames
/// retain the Dawn device/instance for retirement after the UI renderer stops; new publications
/// then return DEVICE_UNAVAILABLE. Stop/join producers and clear images when the app exits,
/// retaining callback state until every accepted frame has retired.
///
/// @param image The image to show the frame in.
/// @param frame The frame; borrowed for the call (its handles until the release callback).
/// @return ELG_GPU_IMAGE_OK when the frame was accepted, else the reason it wasn't:
///   INVALID_ARGUMENT, DEVICE_UNAVAILABLE, UNSUPPORTED, IMPORT_FAILED or INTERNAL_ERROR.
ELG_API uint32_t elg_gpu_image_publish(ElgGpuImage* image, const ElgGpuImageFrame* frame);
/// Stops showing the current frame (it's released once nothing draws it). The natural size stays.
/// Thread-safe like elg_gpu_image_publish().
///
/// @param image The image, or null (nothing happens).
ELG_API void elg_gpu_image_clear(ElgGpuImage* image);

#ifdef __cplusplus
} // extern "C"

#include <elg/view.h>
#include <memory>
namespace elg {
namespace detail { class VideoSource; }
/// Internal bridge for native view plumbing; the returned reference pins the source.
///
/// @param image The image, or null.
/// @return The image's frame source, or null for a null `image`.
ELG_API std::shared_ptr<detail::VideoSource> gpu_image_source(const ElgGpuImage* image);
/// Internal bridge for native view plumbing.
///
/// @param image The image, or null.
/// @return Whether `image` was created with ELG_GPU_IMAGE_BOTTOM_LEFT; false for null.
ELG_API bool gpu_image_flipped(const ElgGpuImage* image);
/// Shows the frames published to `image`, on black. Without a proposed size, it takes the image's
/// natural size.
///
/// The view retains its source independently of the C handle. Rust view shims can call this;
/// the input/ownership/synchronization API itself requires no C++ app code or Dawn ABI types.
///
/// @param image The image to show.
/// @param mode ContentMode::fit shows all of each frame; ContentMode::fill crops it to the view.
/// @return The view, or an empty view for a null `image`.
ELG_API View GpuImageView(const ElgGpuImage* image, ContentMode mode = ContentMode::fit);
}
#endif
#endif
