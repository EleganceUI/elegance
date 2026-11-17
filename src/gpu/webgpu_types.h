// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_WEBGPU_TYPES_H
#define ELG_GPU_WEBGPU_TYPES_H

// WebGPU's types as the GPU layer's headers name them: declared, not defined, so that the headers
// other layers include don't include WebGPU. Source files that use them include
// <webgpu/webgpu_cpp.h>, whose definitions must match these (the compiler checks the enums'
// underlying types).

#include <cstdint>

namespace wgpu {

// NOLINTBEGIN(bugprone-forward-declaration-namespace): WebGPU's own, which the GPU layer's
// classes of the same names (gpu::Device, gpu::Instance, gpu::Buffer) wrap.
class Adapter;
class BindGroup;
class BindGroupLayout;
class Buffer;
class CommandBuffer;
class CommandEncoder;
class Device;
class Instance;
class Queue;
class RenderPassEncoder;
class RenderPipeline;
class Sampler;
class ShaderModule;
class SharedFence;
class Surface;
class Texture;
class TextureView;
// NOLINTEND(bugprone-forward-declaration-namespace)

struct Future;
struct InstanceDescriptor;
struct StringView;

enum class BackendType : std::uint32_t;
enum class FeatureName : std::uint32_t;
enum class TextureFormat : std::uint32_t;
enum class BufferUsage : std::uint64_t;
enum class TextureUsage : std::uint64_t;

} // namespace wgpu

#endif // ELG_GPU_WEBGPU_TYPES_H
