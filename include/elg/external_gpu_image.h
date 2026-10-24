// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
#ifndef ELG_EXTERNAL_GPU_IMAGE_H
#define ELG_EXTERNAL_GPU_IMAGE_H

#include <elg/export.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ElgExternalGpuImage ElgExternalGpuImage;
// Fixed-width tags, not C enums, so Rust can use repr(C) without enum validity hazards.
#define ELG_EXTERNAL_OK 0u
#define ELG_EXTERNAL_INVALID_ARGUMENT 1u
#define ELG_EXTERNAL_DEVICE_UNAVAILABLE 2u
#define ELG_EXTERNAL_UNSUPPORTED 3u
#define ELG_EXTERNAL_IMPORT_FAILED 4u
#define ELG_EXTERNAL_SYNC_FAILED 5u
#define ELG_EXTERNAL_INTERNAL_ERROR 6u

#define ELG_EXTERNAL_IOSURFACE 1u
#define ELG_EXTERNAL_DXGI_HANDLE 2u
#define ELG_EXTERNAL_VULKAN_OPAQUE_FD 3u
#define ELG_EXTERNAL_RGBA8 1u
#define ELG_EXTERNAL_BGRA8 2u
#define ELG_EXTERNAL_TOP_LEFT 0u
#define ELG_EXTERNAL_BOTTOM_LEFT 1u

#define ELG_EXTERNAL_FENCE_NONE 0u
#define ELG_EXTERNAL_FENCE_MTL_SHARED_EVENT 1u
#define ELG_EXTERNAL_FENCE_DXGI_HANDLE 2u
#define ELG_EXTERNAL_FENCE_VK_OPAQUE_FD 3u

typedef struct ElgExternalGpuFence {
    uint32_t type;
    void* handle; // id<MTLSharedEvent> or shared D3D fence NT HANDLE; not a WebGPU handle
    int fd;       // VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT, binary semaphore
    uint64_t value; // Metal/D3D timeline value; Vulkan binary semaphore: 1
} ElgExternalGpuFence;

typedef struct ElgExternalGpuImageRelease {
    uint32_t status;
    uint64_t token; // unchanged producer token, not used as a cache identity
    const ElgExternalGpuFence* fences;
    size_t fence_count;
    int32_t vk_old_layout;
    int32_t vk_new_layout;
} ElgExternalGpuImageRelease;

typedef void (*ElgExternalGpuImageReleaseCallback)(void* userdata, const ElgExternalGpuImageRelease* release);

typedef struct ElgExternalGpuImageFrame {
    uint32_t transport;
    uint32_t format;
    uint32_t width;
    uint32_t height;
    void* handle; // IOSurfaceRef (not IOSurfaceID), or DXGI texture NT HANDLE
    int memory_fd;
    // Vulkan only: exact VkImageCreateInfo including its pNext chain, matching the exported
    // image bound at allocation offset zero (no suballocations). Borrowed for publish() only.
    // Same physical device/compatible memory type required.
    const void* vk_image_create_info;
    uint32_t vk_memory_type_index;
    uint64_t vk_allocation_size;
    uint32_t vk_dedicated_allocation; // 0 or 1
    int32_t vk_old_layout;
    int32_t vk_new_layout;
    ElgExternalGpuFence producer_fence;
    uint64_t token;
    void* userdata;
    ElgExternalGpuImageReleaseCallback release;
} ElgExternalGpuImageFrame;

// Unsafe, low-level native sharing API. No CPU upload/readback and no conversion fallback.
// Publication requires a live renderer. Fixed origin; one mip/layer/sample, 2D RGBA/BGRA
// UNORM storage, sRGB-encoded RGB, straight alpha. Import must permit an sRGB sampling view.
// Create dimensions must be nonzero and supply the initial natural size until a frame is accepted.
// Each publication may have different nonzero dimensions; successful frames update natural size
// asynchronously on the UI thread without replacing the source. Clear retains the last natural size.
// No YUV, keyed mutex, DMA-BUF, cross-adapter or device-loss recovery support.
ELG_API ElgExternalGpuImage* elg_external_gpu_image_create(uint32_t width, uint32_t height, uint32_t origin);
ELG_API void elg_external_gpu_image_destroy(ElgExternalGpuImage* image);
// Thread-safe against publish/clear on a LIVE handle, not against destroy. Synchronous import;
// returns a status above. On failure ownership stays with caller, no callback and no reads;
// the previous frame and natural size remain unchanged.
// On success, exactly one release callback follows, possibly before publish returns if another
// thread clears/replaces the frame. Keep userdata/resources alive until that callback.
//
// Each accepted publication is a fresh, exclusive allocation: never publish aliases of an
// outstanding allocation. Stop all producer writes before publication. Supply its producer
// fence, or NONE as an explicit assertion that producer GPU writes have ALREADY COMPLETED.
// Handles/FDs are borrowed, never closed/released by Elegance; keep them and the allocation
// alive until release. A Vulkan semaphore must be exportable/importable as a BINARY semaphore.
// Producer must release external queue-family ownership as required by Dawn's Vulkan sharing
// contract and supply the corresponding old/new layouts. Elegance returns the reverse layout
// transition at retirement; producer must acquire ownership before accessing the image again.
//
// Release means NO FUTURE consumer submissions, NOT GPU completion. All returned fences must
// be imported/waited before overwriting, recycling OR freeing the allocation. Fence handles
// and the release record are BORROWED ONLY DURING CALLBACK: duplicate/retain them there.
// A consuming native import (e.g. vkImportSemaphoreFdKHR) must consume a DUPLICATE, not the
// borrowed exported FD/handle.
// On non-OK release, synchronization is unknown: quarantine the allocation until independently
// establishing GPU completion/device teardown; do not infer safety from an empty fence list.
// Retirement normally runs asynchronously on an internal worker; clear/destroy do not drain
// callbacks. Metal scheduling waits are bounded (5 seconds), then report SYNC_FAILED.
// Callback may run on any thread, must not throw/unwind, block waiting for renderer work, or
// reenter this API. destroy is not a drain: views/snapshots may retain frames. Accepted frames
// retain the Dawn device/instance for retirement after the UI renderer stops; new publications
// then return DEVICE_UNAVAILABLE. Stop/join producers and clear images when the app exits,
// retaining callback state until every accepted frame has retired.
ELG_API uint32_t elg_external_gpu_image_publish(ElgExternalGpuImage* image, const ElgExternalGpuImageFrame* frame);
ELG_API void elg_external_gpu_image_clear(ElgExternalGpuImage* image);

#ifdef __cplusplus
} // extern "C"

#include <elg/view.h>
#include <memory>
namespace elg {
namespace detail { class VideoSource; }
// Internal bridge for native view plumbing; the returned reference pins the source.
ELG_API std::shared_ptr<detail::VideoSource> external_gpu_image_source(const ElgExternalGpuImage* image);
ELG_API bool external_gpu_image_flipped(const ElgExternalGpuImage* image);
// The view retains its source independently of the C handle. Rust view shims can call this;
// the input/ownership/synchronization API itself requires no C++ app code or Dawn ABI types.
ELG_API View ExternalGpuImageView(const ElgExternalGpuImage* image, ContentMode mode = ContentMode::fit);
}
#endif
#endif
