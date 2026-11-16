// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_WEBGPU_TYPES_H
#define ELG_GPU_WEBGPU_TYPES_H

// WebGPU's types as the GPU layer's headers name them: declared, not defined, so that the headers
// other layers include don't include WebGPU. Source files that use them include
// <webgpu/webgpu_cpp.h> (in the GPU layer, gpu/webgpu.h), whose definitions must match these.

#include <cstdint>

namespace wgpu {

class Device;
class Instance;
class Surface;
class Texture;
class TextureView;

struct InstanceDescriptor;

enum class TextureFormat : std::uint32_t;

} // namespace wgpu

#endif // ELG_GPU_WEBGPU_TYPES_H
