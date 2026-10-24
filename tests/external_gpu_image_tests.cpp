// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
#include <elg/external_gpu_image.h>
#include "core/ui_tasks.h"
#include "media/video_source.h"
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
    REQUIRE(source);
    CHECK(source->size.peek().width == 16);
    CHECK(source->size.peek().height == 16);
    const auto source_id = source->id;
    auto view = elg::ExternalGpuImageView(image);
    auto another_window = elg::ExternalGpuImageView(image, elg::ContentMode::fill);
    elg_external_gpu_image_clear(image);
    CHECK(elg::external_gpu_image_source(image) == source);
    CHECK(source->id == source_id);
    CHECK(source->size.peek().width == 16);
    CHECK(source->size.peek().height == 16);
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
    frame.width = 0;
    CHECK(elg_external_gpu_image_publish(image.get(), &frame) == ELG_EXTERNAL_INVALID_ARGUMENT);
    frame.width = 16;
    frame.height = 0;
    CHECK(elg_external_gpu_image_publish(image.get(), &frame) == ELG_EXTERNAL_INVALID_ARGUMENT);
    frame.height = 16;
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
    auto source = elg::external_gpu_image_source(image.get());
    const auto source_id = source->id;
    for (const auto dimensions : {elg::Size{17, 16}, elg::Size{16, 23}, elg::Size{64, 32}}) {
        frame.width = static_cast<uint32_t>(dimensions.width);
        frame.height = static_cast<uint32_t>(dimensions.height);
        // Different dimensions pass validation, but cannot change metadata unless import
        // succeeds. Without a renderer, no fake handle reaches the native API.
        CHECK(elg_external_gpu_image_publish(image.get(), &frame) == ELG_EXTERNAL_DEVICE_UNAVAILABLE);
        CHECK(elg::external_gpu_image_source(image.get()) == source);
        CHECK(source->id == source_id);
        CHECK_FALSE(source->current());
        CHECK(source->size.peek().width == 16);
        CHECK(source->size.peek().height == 16);
    }
    elg::detail::run_ui_tasks();
    CHECK(source->size.peek().width == 16);
    CHECK(source->size.peek().height == 16);
    image.reset();
    CHECK(callbacks == 0);
}

#ifdef __APPLE__
#include "gpu/device.h"
#include <IOSurface/IOSurface.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace {
struct TestSurface {
    IOSurfaceRef surface;
    std::shared_ptr<std::atomic<unsigned>> releases;
};

std::uint32_t publish_surface(ElgExternalGpuImage* image, std::uint32_t width,
                              std::uint32_t height,
                              const std::shared_ptr<std::atomic<unsigned>>& releases,
                              bool mismatch = false) {
    const void* keys[] = {kIOSurfaceWidth, kIOSurfaceHeight, kIOSurfaceBytesPerElement,
                          kIOSurfaceBytesPerRow, kIOSurfacePixelFormat};
    const int values[] = {static_cast<int>(width), static_cast<int>(height), 4,
                           static_cast<int>(width * 4), 0x42475241}; // BGRA
    CFNumberRef numbers[5];
    const void* entries[5];
    for (unsigned i = 0; i != 5; ++i)
        entries[i] = numbers[i] = CFNumberCreate(nullptr, kCFNumberIntType, &values[i]);
    auto properties = CFDictionaryCreate(nullptr, keys, entries, 5,
                                         &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    auto surface = IOSurfaceCreate(properties);
    CFRelease(properties);
    for (auto number : numbers) CFRelease(number);
    if (!surface) return ELG_EXTERNAL_INTERNAL_ERROR;
    IOSurfaceLock(surface, 0, nullptr);
    std::memset(IOSurfaceGetBaseAddress(surface), 0, IOSurfaceGetAllocSize(surface));
    IOSurfaceUnlock(surface, 0, nullptr);
    auto payload = std::make_unique<TestSurface>(TestSurface{surface, releases});
    ElgExternalGpuImageFrame frame{};
    frame.transport = ELG_EXTERNAL_IOSURFACE;
    frame.format = ELG_EXTERNAL_BGRA8;
    frame.width = width + (mismatch ? 1 : 0);
    frame.height = height;
    frame.handle = surface;
    frame.userdata = payload.get();
    frame.release = [](void* user, const ElgExternalGpuImageRelease*) {
        auto retired = std::unique_ptr<TestSurface>(static_cast<TestSurface*>(user));
        // These tests never submit consumer GPU work: no allocation has pending GPU reads.
        CFRelease(retired->surface);
        ++*retired->releases;
    };
    const auto status = elg_external_gpu_image_publish(image, &frame);
    if (status == ELG_EXTERNAL_OK) payload.release();
    else CFRelease(surface);
    return status;
}
}

TEST_CASE("external GPU image resizing coalesces UI metadata without retaining frames",
          "[.gpu][external-gpu-image]") {
    using namespace elg::detail;
    gpu::Instance instance;
    gpu::Device device(instance);
    const auto renderer = gpu::render_device();
    if (renderer->backend != wgpu::BackendType::Metal ||
        !renderer->device.HasFeature(wgpu::FeatureName::SharedTextureMemoryIOSurface) ||
        !renderer->device.HasFeature(wgpu::FeatureName::SharedFenceMTLSharedEvent))
        SKIP("Metal external sharing is unavailable");
    run_ui_tasks();
    auto image = std::unique_ptr<ElgExternalGpuImage, decltype(&elg_external_gpu_image_destroy)>(
        elg_external_gpu_image_create(16, 16, ELG_EXTERNAL_BOTTOM_LEFT), elg_external_gpu_image_destroy);
    REQUIRE(image);
    auto source = elg::external_gpu_image_source(image.get());
    const auto source_id = source->id;
    auto releases = std::make_shared<std::atomic<unsigned>>(0);
    auto wakes = std::make_shared<std::atomic<unsigned>>(0);
    struct ResetWaker { ~ResetWaker() { set_ui_task_waker({}); } } reset_waker;
    set_ui_task_waker([wakes] { ++*wakes; });
    unsigned size_notifications = 0;
    Scope scope;
    Scope::Use use(scope);
    elg::effect([size = source->size, &size_notifications] {
        (void)size.get();
        ++size_notifications;
    });
    auto publish = [&](unsigned width, unsigned height, bool mismatch = false) {
        std::uint32_t status = ELG_EXTERNAL_INTERNAL_ERROR;
        std::thread producer([&] { status = publish_surface(image.get(), width, height, releases, mismatch); });
        producer.join();
        return status;
    };
    REQUIRE(publish(32, 24) == ELG_EXTERNAL_OK);
    auto first = source->current();
    REQUIRE(first);
    CHECK(first->width == 32);
    CHECK(first->height == 24);
    REQUIRE(publish(48, 40) == ELG_EXTERNAL_OK);
    REQUIRE(publish(64, 48) == ELG_EXTERNAL_OK);
    CHECK(source->size.peek().width == 16);
    CHECK(source->size.peek().height == 16);
    CHECK(wakes->load() == 1); // the entire resize burst shares one pending task
    const auto retirement_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (releases->load() != 1 && std::chrono::steady_clock::now() < retirement_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(releases->load() == 1); // queued metadata did not retain the replaced 48x40 frame
    CHECK(source->id == source_id);
    CHECK(elg::external_gpu_image_source(image.get()) == source);
    CHECK(elg::external_gpu_image_flipped(image.get()));
    REQUIRE(source->current());
    CHECK(source->current()->width == 64);
    CHECK(source->current()->height == 48);
    CHECK(source->current()->source_id == source_id);
    CHECK(source->current()->texture_id != first->texture_id);
    CHECK(run_ui_tasks());
    CHECK(source->size.peek().width == 64);
    CHECK(source->size.peek().height == 48);
    flush_effects();
    CHECK(size_notifications == 2);
    CHECK_FALSE(run_ui_tasks());
    REQUIRE(publish(64, 48) == ELG_EXTERNAL_OK);
    CHECK_FALSE(run_ui_tasks()); // same-size frames do not enqueue metadata work
    const auto previous_texture = source->current()->texture_id;
    CHECK(publish(80, 56, true) == ELG_EXTERNAL_IMPORT_FAILED);
    CHECK(source->current()->texture_id == previous_texture);
    CHECK_FALSE(run_ui_tasks());
    CHECK(source->size.peek().width == 64);
    REQUIRE(publish(80, 56) == ELG_EXTERNAL_OK);
    REQUIRE(publish(64, 48) == ELG_EXTERNAL_OK); // pending resize returns to the current UI size
    CHECK(run_ui_tasks());
    CHECK(source->size.peek().width == 64);
    CHECK(source->size.peek().height == 48);
    flush_effects();
    CHECK(size_notifications == 2); // returning to the UI size does not notify observers
    REQUIRE(publish(96, 72) == ELG_EXTERNAL_OK);
    elg_external_gpu_image_clear(image.get());
    CHECK_FALSE(source->current());
    CHECK(run_ui_tasks());
    CHECK(source->size.peek().width == 96);
    CHECK(source->size.peek().height == 72); // clear retains last accepted natural size
    flush_effects();
    CHECK(size_notifications == 3);
    first.reset();
    REQUIRE(publish(112, 80) == ELG_EXTERNAL_OK);
    std::weak_ptr<VideoSource> weak = source;
    image.reset();
    source.reset();
    CHECK(weak.expired()); // queued metadata must not retain the source or its final frame
    CHECK(run_ui_tasks());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (releases->load() != 8 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(releases->load() == 8);
}
#endif
