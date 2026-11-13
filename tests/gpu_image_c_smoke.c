// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// Standalone C11 ABI smoke test; link the compiled object with the native Elegance library.
#include <elg/gpu_image.h>
#include <stdio.h>

// Not assert: the checks, and the calls inside them, must run in release builds too.
static int failures = 0;
#define CHECK(condition)                                                                     \
    do {                                                                                     \
        if (!(condition)) {                                                                  \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                                      \
        }                                                                                    \
    } while (0)

static void released(void* userdata, const ElgGpuImageRelease* result) {
    (void)result;
    ++*(unsigned*)userdata;
}

int main(void) {
    CHECK(!elg_gpu_image_create(0, 16, ELG_GPU_IMAGE_TOP_LEFT));
    CHECK(!elg_gpu_image_create(16, 0, ELG_GPU_IMAGE_TOP_LEFT));
    CHECK(!elg_gpu_image_create(16, 16, 99));
    elg_gpu_image_clear(NULL);
    elg_gpu_image_destroy(NULL);
    CHECK(elg_gpu_image_publish(NULL, NULL) == ELG_GPU_IMAGE_INVALID_ARGUMENT);
    ElgGpuImage* image = elg_gpu_image_create(16, 16, ELG_GPU_IMAGE_BOTTOM_LEFT);
    CHECK(image);
    unsigned callbacks = 0;
    ElgGpuImageFrame frame = {0};
    frame.width = frame.height = 16;
    frame.format = ELG_GPU_IMAGE_RGBA8;
    frame.transport = ELG_GPU_IMAGE_IOSURFACE;
    frame.handle = &callbacks; // no renderer: this must never be passed to Dawn
    frame.userdata = &callbacks;
    frame.release = released;
    CHECK(elg_gpu_image_publish(image, NULL) == ELG_GPU_IMAGE_INVALID_ARGUMENT);
    frame.width = 0; // a frame may change size, but never to nothing
    CHECK(elg_gpu_image_publish(image, &frame) == ELG_GPU_IMAGE_INVALID_ARGUMENT);
    frame.width = 16;
    frame.release = NULL;
    CHECK(elg_gpu_image_publish(image, &frame) == ELG_GPU_IMAGE_INVALID_ARGUMENT);
    frame.release = released;
    frame.format = 99;
    CHECK(elg_gpu_image_publish(image, &frame) == ELG_GPU_IMAGE_INVALID_ARGUMENT);
    frame.format = ELG_GPU_IMAGE_RGBA8;
    frame.producer_fence.type = ELG_GPU_IMAGE_FENCE_DXGI_HANDLE;
    frame.producer_fence.handle = &callbacks;
    CHECK(elg_gpu_image_publish(image, &frame) == ELG_GPU_IMAGE_INVALID_ARGUMENT);
    frame.producer_fence.type = ELG_GPU_IMAGE_FENCE_NONE;
    frame.transport = ELG_GPU_IMAGE_VULKAN_OPAQUE_FD;
    frame.memory_fd = -1;
    CHECK(elg_gpu_image_publish(image, &frame) == ELG_GPU_IMAGE_INVALID_ARGUMENT);
    frame.transport = ELG_GPU_IMAGE_IOSURFACE;
    CHECK(elg_gpu_image_publish(image, &frame) == ELG_GPU_IMAGE_DEVICE_UNAVAILABLE);
    elg_gpu_image_clear(image);
    elg_gpu_image_destroy(image);
    CHECK(callbacks == 0);
    if (failures) {
        fprintf(stderr, "external GPU image C11 ABI smoke test: %d checks failed\n", failures);
        return 1;
    }
    puts("external GPU image C11 ABI smoke test passed");
    return 0;
}
