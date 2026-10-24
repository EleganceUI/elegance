// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// Standalone C11 ABI smoke test; link the compiled object with the native Elegance library.
#include <elg/external_gpu_image.h>
#include <assert.h>
#include <stdio.h>

static void released(void* userdata, const ElgExternalGpuImageRelease* result) {
    (void)result;
    ++*(unsigned*)userdata;
}

int main(void) {
    assert(!elg_external_gpu_image_create(0, 16, ELG_EXTERNAL_TOP_LEFT));
    assert(!elg_external_gpu_image_create(16, 0, ELG_EXTERNAL_TOP_LEFT));
    assert(!elg_external_gpu_image_create(16, 16, 99));
    elg_external_gpu_image_clear(NULL);
    elg_external_gpu_image_destroy(NULL);
    assert(elg_external_gpu_image_publish(NULL, NULL) == ELG_EXTERNAL_INVALID_ARGUMENT);
    ElgExternalGpuImage* image = elg_external_gpu_image_create(16, 16, ELG_EXTERNAL_BOTTOM_LEFT);
    assert(image);
    unsigned callbacks = 0;
    ElgExternalGpuImageFrame frame = {0};
    frame.width = frame.height = 16;
    frame.format = ELG_EXTERNAL_RGBA8;
    frame.transport = ELG_EXTERNAL_IOSURFACE;
    frame.handle = &callbacks; // no renderer: this must never be passed to Dawn
    frame.userdata = &callbacks;
    frame.release = released;
    assert(elg_external_gpu_image_publish(image, NULL) == ELG_EXTERNAL_INVALID_ARGUMENT);
    frame.width = 17;
    assert(elg_external_gpu_image_publish(image, &frame) == ELG_EXTERNAL_INVALID_ARGUMENT);
    frame.width = 16;
    frame.release = NULL;
    assert(elg_external_gpu_image_publish(image, &frame) == ELG_EXTERNAL_INVALID_ARGUMENT);
    frame.release = released;
    frame.format = 99;
    assert(elg_external_gpu_image_publish(image, &frame) == ELG_EXTERNAL_INVALID_ARGUMENT);
    frame.format = ELG_EXTERNAL_RGBA8;
    frame.producer_fence.type = ELG_EXTERNAL_FENCE_DXGI_HANDLE;
    frame.producer_fence.handle = &callbacks;
    assert(elg_external_gpu_image_publish(image, &frame) == ELG_EXTERNAL_INVALID_ARGUMENT);
    frame.producer_fence.type = ELG_EXTERNAL_FENCE_NONE;
    frame.transport = ELG_EXTERNAL_VULKAN_OPAQUE_FD;
    frame.memory_fd = -1;
    assert(elg_external_gpu_image_publish(image, &frame) == ELG_EXTERNAL_INVALID_ARGUMENT);
    frame.transport = ELG_EXTERNAL_IOSURFACE;
    assert(elg_external_gpu_image_publish(image, &frame) == ELG_EXTERNAL_DEVICE_UNAVAILABLE);
    elg_external_gpu_image_clear(image);
    elg_external_gpu_image_destroy(image);
    assert(callbacks == 0);
    puts("external GPU image C11 ABI smoke test passed");
    return 0;
}
