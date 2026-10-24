// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
#include <elg/external_gpu_image.h>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <type_traits>

static_assert(std::is_standard_layout_v<ElgExternalGpuImageFrame>);
static_assert(std::is_trivially_copyable_v<ElgExternalGpuImageFrame>);
static_assert(std::is_standard_layout_v<ElgExternalGpuImageRelease>);

TEST_CASE("external GPU image C handles and view references", "[external-gpu-image]") {
    CHECK(elg_external_gpu_image_create(0, 16, ELG_EXTERNAL_TOP_LEFT) == nullptr);
    CHECK(elg_external_gpu_image_create(16, 0, ELG_EXTERNAL_TOP_LEFT) == nullptr);
    CHECK(elg_external_gpu_image_create(16, 16, 99) == nullptr);
    elg_external_gpu_image_destroy(nullptr);
    elg_external_gpu_image_clear(nullptr);
    CHECK(elg_external_gpu_image_publish(nullptr, nullptr) == ELG_EXTERNAL_INVALID_ARGUMENT);
    CHECK_NOTHROW(elg::ExternalGpuImageView(nullptr));

    auto* image = elg_external_gpu_image_create(16, 16, ELG_EXTERNAL_BOTTOM_LEFT);
    REQUIRE(image);
    CHECK(elg::external_gpu_image_flipped(image));
    auto source = elg::external_gpu_image_source(image);
    CHECK(source);
    auto view = elg::ExternalGpuImageView(image);
    auto another_window = elg::ExternalGpuImageView(image, elg::ContentMode::fill);
    elg_external_gpu_image_clear(image);
    elg_external_gpu_image_destroy(image);
    // Native views and their source survive destruction of the FFI handle.
    CHECK(source.use_count() >= 3);
}

TEST_CASE("rejected external publications never transfer ownership", "[external-gpu-image]") {
    auto image = std::unique_ptr<ElgExternalGpuImage, decltype(&elg_external_gpu_image_destroy)>(
        elg_external_gpu_image_create(16, 16, ELG_EXTERNAL_TOP_LEFT), elg_external_gpu_image_destroy);
    REQUIRE(image);
    unsigned callbacks = 0;
    ElgExternalGpuImageFrame frame{};
    frame.width = frame.height = 16;
    frame.transport = ELG_EXTERNAL_IOSURFACE;
    frame.format = ELG_EXTERNAL_RGBA8;
    // Deliberately fake native handles: validation must reject before touching Dawn/native APIs.
    frame.handle = &callbacks;
    frame.userdata = &callbacks;
    frame.release = [](void* user, const ElgExternalGpuImageRelease*) { ++*static_cast<unsigned*>(user); };
    CHECK(elg_external_gpu_image_publish(image.get(), nullptr) == ELG_EXTERNAL_INVALID_ARGUMENT);
    frame.width = 17;
    CHECK(elg_external_gpu_image_publish(image.get(), &frame) == ELG_EXTERNAL_INVALID_ARGUMENT);
    frame.width = 16;
    frame.producer_fence.type = ELG_EXTERNAL_FENCE_DXGI_HANDLE;
    frame.producer_fence.handle = &callbacks;
    CHECK(elg_external_gpu_image_publish(image.get(), &frame) == ELG_EXTERNAL_INVALID_ARGUMENT);
    frame.producer_fence.type = ELG_EXTERNAL_FENCE_NONE;
    frame.transport = ELG_EXTERNAL_VULKAN_OPAQUE_FD;
    frame.memory_fd = -1;
    CHECK(elg_external_gpu_image_publish(image.get(), &frame) == ELG_EXTERNAL_INVALID_ARGUMENT);
    frame.transport = ELG_EXTERNAL_IOSURFACE;
    // This test executable has no App/renderer; a syntactically valid frame is not imported.
    CHECK(elg_external_gpu_image_publish(image.get(), &frame) == ELG_EXTERNAL_DEVICE_UNAVAILABLE);
    image.reset();
    CHECK(callbacks == 0);
}
