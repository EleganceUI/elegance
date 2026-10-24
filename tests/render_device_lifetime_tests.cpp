// This Source Code Form is subject to the terms of the Mozilla Public License,
// v. 2.0.
#include "gpu/device.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

TEST_CASE("accepted frames can retain a usable device after renderer shutdown",
          "[.gpu][external-gpu-image]") {
  using namespace elg::detail::gpu;
  std::shared_ptr<RenderDevice> retained;
  {
    Instance instance;
    Device device(instance);
    retained = render_device();
    REQUIRE(retained);
    REQUIRE(retained->active);
  }
  CHECK_FALSE(render_device());
  CHECK_FALSE(retained->active);

  // EndAccess may schedule work and return a scheduling future. Both the device
  // and instance must remain usable, rather than just retaining destroyed
  // handles.
  wgpu::BufferDescriptor descriptor;
  descriptor.size = 4;
  descriptor.usage = wgpu::BufferUsage::CopyDst;
  auto buffer = retained->device.CreateBuffer(&descriptor);
  auto encoder = retained->device.CreateCommandEncoder();
  encoder.ClearBuffer(buffer, 0, 4);
  auto commands = encoder.Finish();
  retained->queue.Submit(1, &commands);

  auto status = std::make_shared<wgpu::QueueWorkDoneStatus>(
      wgpu::QueueWorkDoneStatus::Error);
  auto future = retained->queue.OnSubmittedWorkDone(
      wgpu::CallbackMode::WaitAnyOnly,
      [status](wgpu::QueueWorkDoneStatus value, wgpu::StringView) {
        *status = value;
      });
  REQUIRE(retained->instance.WaitAny(future, 5'000'000'000ull) ==
          wgpu::WaitStatus::Success);
  CHECK(*status == wgpu::QueueWorkDoneStatus::Success);
}
