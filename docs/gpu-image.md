# GPU image input (native / Rust FFI)

`include/elg/gpu_image.h` provides the native GPU image API for shared cross-device external backing. It succeeds the removed same-device Dawn `GpuImage`/`GpuFrame` API; it does not accept Dawn textures or expose that old API. The C portion is valid C11 and can be bound directly by Rust (`repr(C)` descriptors, `u32` tags, opaque pointer, `extern "C"` callback). No Dawn, Vulkan, Metal or Windows headers are needed to include it. Native transport handles still require the producer's platform API.

## Entry points

- `elg_gpu_image_create(width, height, origin)` creates an opaque handle; null means invalid dimensions/origin or allocation failure. Creating an image does not require a renderer.
- `elg_gpu_image_publish(image, &frame)` synchronously imports a fresh allocation and begins Dawn access. A renderer must already exist. Returns `OK`, `INVALID_ARGUMENT`, `DEVICE_UNAVAILABLE`, `UNSUPPORTED`, `IMPORT_FAILED`, or `INTERNAL_ERROR`. No ownership transfer/callback occurs on rejection.
- `elg_gpu_image_clear(image)` retires the current frame, subject to outstanding snapshots/recordings.
- `elg_gpu_image_destroy(image)` releases the FFI handle, not references owned by views. It is not a GPU drain.
- Native `elg::GpuImageView(image, mode)` retains the source for layout/rendering. A Rust view binding can call it from its existing native shim; no C++ application/backend implementation is required. This change does **not** add Rust bindings or a C representation of `elg::View`.

`ElgGpuImageFrame` contains the transport, format, dimensions, native allocation handle/FD, Vulkan metadata/layouts, producer fence, producer token, userdata, and a mandatory release callback. `ElgGpuImageRelease` returns the token, status, **all** consumer fence handles/values, and Vulkan old/new layouts. Frame format is RGBA8 or BGRA8 UNORM storage with sRGB-encoded RGB and straight alpha, sampled directly through an sRGB view. Origin is fixed per image. Each accepted publication may have different nonzero dimensions; natural size updates asynchronously on the UI thread, and clear retains the last accepted natural size.

## Implemented import paths and limits

| Transport | Required Dawn features | Synchronization | Restrictions |
| --- | --- | --- | --- |
| IOSurfaceRef | `SharedTextureMemoryIOSurface`, `SharedFenceMTLSharedEvent` | `id<MTLSharedEvent>` + timeline value | Metal backend; single-plane RGBA/BGRA IOSurface. A pointer, not IOSurfaceID or Mach port. |
| DXGI texture NT handle | `SharedTextureMemoryDXGISharedHandle`, `SharedFenceDXGISharedHandle` | shared D3D fence NT handle + timeline value | D3D12 backend only; resource must allow sRGB sampling; no keyed mutex. |
| Vulkan opaque memory FD | `SharedTextureMemoryOpaqueFD`, `SharedFenceVkSemaphoreOpaqueFD` | opaque FD for an exportable **binary** VkSemaphore, value 1 | Vulkan backend; exact `VkImageCreateInfo`/pNext, memory type index, allocation size/dedicated flag, and sharing layouts must match producer allocation. |

All features are enabled at renderer device creation **only if the adapter advertises them**. `SharedTextureMemoryVkDedicatedAllocation` is also optionally enabled. Publication requires both memory import and native fence features even if the producer asserts its writes have already completed, because consumer fence export is necessary. Backend/feature mismatch returns `UNSUPPORTED`; incompatible native allocations, views, producer fences, or BeginAccess validation return `IMPORT_FAILED`. Bad native pointers are an unsafe-contract violation, not recoverable validation.

These are implemented paths, not a claim that every Dawn build/driver supports them. The macOS native target and a live headless Metal import/sampling/fence round trip were validated on an Apple M4 Pro. Windows/Linux runtime imports require platform testing. D3D11, OpenGL/GLES, YUV/multi-plane images, DMA-BUF, cross-adapter sharing and device-loss recovery are not supported by this API. There is no CPU-copy or GPU-copy fallback.

The producer must choose the same physical GPU and compatible export parameters. This API does not expose physical-adapter negotiation or construct producer resources. For Vulkan, follow Dawn SharedTextureMemory's external queue-family ownership contract: release to the external family and provide the old/new layout transition; use the returned transition on reacquire. The image must be bound at allocation offset zero; suballocated offsets are not represented by this API. Raw Vulkan structures must match the platform Vulkan ABI and are borrowed only for the synchronous publish call.

## Ownership and reuse protocol

1. Produce a **fresh exclusive allocation** (or a pool slot whose previous release fences have all completed). Submit all writes and release native ownership. Never publish aliases while a slot is outstanding.
2. Supply the producer fence. `FENCE_NONE` is an unsafe assertion that all producer GPU writes **already completed**, not merely that they were submitted.
3. Call publish. On failure, producer retains ownership and no consumer reads occurred. On success, allocation, native input handles/FDs and callback state must remain valid until release. Elegance does not close the caller's handles/FDs.
4. Elegance imports once and holds a single exclusive Dawn access open for the entire immutable frame lifetime. The source and **each** `VideoFrame` snapshot retain the allocation. `VideoTextures` additionally pins it from acquire through command submission, counting each distinct outstanding batch independently (repeated acquires within one batch count once). Each batch must be released exactly once after submission. Advancing another window/source cannot release an older snapshot. Repeated windows/frames use the same access, not multiple overlapping BeginAccess calls.
5. Clear/replacement removes the source's reference. Once every snapshot and recording releases its reference, an internal retirement worker retains the imported objects and performs EndAccess/export. The render/UI thread does not wait for retirement. Metal additionally waits for Dawn's `commandsScheduledFuture` before returning the event, **not** for GPU completion. Scheduling and error-scope waits are bounded at five seconds; retirement waits and foreign callbacks hold no renderer/source mutex. Timeouts report `SYNC_FAILED` and require quarantine.
6. The callback may run on any thread and can outlive the C handle. Its release record, fence array and native exported handles are borrowed **only during the call**. Retain/duplicate every fence there (Metal retain, Windows DuplicateHandle, Unix dup). A consuming import such as `vkImportSemaphoreFdKHR` must consume a duplicate, not the borrowed FD. Do not close a borrowed handle. Do not unwind, reenter the API, or block waiting for renderer work.
7. Wait on **every** returned dependency before writing, recycling, or freeing the producer allocation. A successful callback promises no future consumer submissions, **not** completed GPU work. An empty successful dependency list means Dawn returned no pending dependency. Retired-before-display frames also get exactly one callback.
8. Non-OK retirement (`SYNC_FAILED`, `DEVICE_UNAVAILABLE`, or `INTERNAL_ERROR`) is **not permission to reuse**: quarantine the allocation until independently establishing GPU completion or device teardown. Never treat failed/unknown export as an empty successful fence list.

Publish/clear may be concurrent on a live handle. Destroy must be externally synchronized. Callbacks are outside source/device locks. Remove views, clear images, and stop/join producer threads before renderer shutdown; keep callback state alive for delayed retirement. On aborted rendering, leases remain held through cache destruction; aborted recordings must never be submitted afterwards (same rule as existing `VideoTextures`).

## Validation

The native `elegance` target builds against the installed Dawn headers in `build/local-offline-static/vcpkg_installed/arm64-osx/include/dawn`. Tests cover descriptor rejection/ownership, opaque-handle/view lifetime, and C header/ABI usage without creating a renderer.

Run the native targets and GPU image tests from the native repo (the `[gpu-image]` filter also explicitly selects hidden GPU tests):

```sh
cmake --build build/local-offline-static --target elegance elegance_tests gpu_image_c_smoke
ctest --test-dir build/local-offline-static --output-on-failure -R gpu_image_c_smoke
build/local-offline-static/bin/elegance_tests '[gpu-image]'
```

An optional ignored native-only headless integration harness is available locally in `build/local-offline-static/gpu_image_metal_smoke.mm`; it is not part of the CMake test targets. The live Apple M4 Pro harness uses an independent native Metal producer device/queue, GPU-writes an IOSurface, publishes a producer event without a CPU completion prewait, samples through the actual Elegance `VideoTextures` path, and checks a red readback pixel. The producer then GPU-waits the exported consumer events, reuses the retired allocation, republishes with a new event value, and verifies a blue readback pixel. It also checks overlapping recorded batches, repeated acquires within a batch, separate window caches, source replacement with a retained old snapshot, consumer event completion, a deliberately stalled producer, nonblocking render-side retirement, renderer mutex availability, import rejection without callbacks, 32 live publications with concurrent retirement/import validation, and explicit `DEVICE_UNAVAILABLE` retirement after renderer destruction.

This is native headless validation, not a Rust `wgpu` producer test or an OS-window presentation test. Windows/D3D12, Linux/Vulkan, physical multi-GPU systems and actual device-loss injection remain unvalidated. The ignored harness is intentionally not a production API or an application example.
