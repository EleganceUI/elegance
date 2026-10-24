// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "gpu/video_textures.h"

#include "core/log.h"

#include <algorithm>

namespace elg::detail::gpu {

namespace {

constexpr std::uint64_t kIdleFrames = 240;
constexpr wgpu::TextureFormat kConvertedFormat = wgpu::TextureFormat::BGRA8Unorm;
constexpr wgpu::TextureFormat kSampledFormat = wgpu::TextureFormat::BGRA8UnormSrgb;

constexpr char kConvertWgsl[] =
#include "shaders/video_convert.wgsl.inc"
    ;

struct ConvertImmediates {
    std::uint32_t format;
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t matrix;
    std::uint32_t full_range;
};

} // namespace

VideoTextures::VideoTextures(const Device& device, const wgpu::BindGroupLayout& image_layout)
    : device_(device), layout_(image_layout), sampler_(create_linear_sampler(device.handle())) {
    const wgpu::Device& dev = device.handle();
    const wgpu::ShaderModule module = create_shader(dev, kConvertWgsl, "video_convert.wgsl");

    wgpu::BindGroupLayoutEntry frame{};
    frame.binding = 0;
    frame.visibility = wgpu::ShaderStage::Fragment;
    frame.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    wgpu::BindGroupLayoutDescriptor frame_info;
    frame_info.label = "video frame";
    frame_info.entryCount = 1;
    frame_info.entries = &frame;
    frame_layout_ = dev.CreateBindGroupLayout(&frame_info);

    wgpu::PipelineLayoutDescriptor layout_info;
    layout_info.label = "video convert";
    layout_info.bindGroupLayoutCount = 1;
    layout_info.bindGroupLayouts = &frame_layout_;
    layout_info.immediateSize = sizeof(ConvertImmediates);
    const wgpu::PipelineLayout layout = dev.CreatePipelineLayout(&layout_info);

    wgpu::ColorTargetState target;
    target.format = kConvertedFormat;
    wgpu::FragmentState fragment;
    fragment.module = module;
    fragment.entryPoint = "fs_main";
    fragment.targetCount = 1;
    fragment.targets = &target;
    wgpu::RenderPipelineDescriptor descriptor;
    descriptor.label = "video convert";
    descriptor.layout = layout;
    descriptor.vertex.module = module;
    descriptor.vertex.entryPoint = "vs_main";
    descriptor.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    descriptor.fragment = &fragment;
    convert_pipeline_ = dev.CreateRenderPipeline(&descriptor);
}

bool VideoTextures::import(const VideoFrame& frame, Texture& texture) {
    if (frame.kind == VideoFrame::Kind::external) {
        if (frame.device.Get() != device_.handle().Get() || !frame.texture ||
            !frame.external_view || !frame.texture_lease) return false;
        texture.texture = frame.texture;
        texture.external = true;
        texture.group = create_image_group(device_.handle(), layout_, frame.external_view, sampler_);
        return true;
    }
    if (frame.kind == VideoFrame::Kind::webgpu) {
        // Ordinary Dawn texture, not external memory. Never try to import another device's
        // handles, even when its backend/physical adapter matches the renderer's.
        if (frame.device.Get() != device_.handle().Get() || !frame.texture || !frame.texture_lease) {
            return false;
        }
        texture.texture = frame.texture;
        wgpu::TextureViewDescriptor view;
        view.format = wgpu::TextureFormat::RGBA8UnormSrgb;
        view.usage = wgpu::TextureUsage::TextureBinding; // sRGB views cannot inherit StorageBinding
        texture.group = create_image_group(device_.handle(), layout_, texture.texture.CreateView(&view), sampler_);
        return true;
    }
    if (!frame.shared_handle || !device_.has_feature(wgpu::FeatureName::SharedTextureMemoryDXGISharedHandle)) {
        return false;
    }
    wgpu::SharedTextureMemoryDXGISharedHandleDescriptor shared;
    shared.handle = frame.shared_handle;
    wgpu::SharedTextureMemoryDescriptor memory;
    memory.nextInChain = &shared;
    memory.label = "video frame";
    texture.memory = device_.handle().ImportSharedTextureMemory(&memory);
    if (!texture.memory) {
        return false;
    }
    // BGRA (or RGBA) UNORM storage, sampled through an sRGB view. Direct3D 11 can't view the
    // decoder's texture (typed B8G8R8A8_UNORM) as sRGB, so there each frame is copied into a
    // texture of Dawn's own, which can be.
    const bool copies = device_.backend() == wgpu::BackendType::D3D11;
    const bool rgba = frame.format == VideoFrame::Format::rgba;
    const wgpu::TextureFormat stored = rgba ? wgpu::TextureFormat::RGBA8Unorm : kConvertedFormat;
    const wgpu::TextureFormat sampled_format = rgba ? wgpu::TextureFormat::RGBA8UnormSrgb : kSampledFormat;
    wgpu::TextureDescriptor descriptor;
    descriptor.label = "video frame";
    descriptor.dimension = wgpu::TextureDimension::e2D;
    descriptor.size = {frame.width, frame.height, 1};
    descriptor.format = stored;
    descriptor.usage = copies ? wgpu::TextureUsage::CopySrc : wgpu::TextureUsage::TextureBinding;
    descriptor.viewFormatCount = copies ? 0 : 1;
    descriptor.viewFormats = &sampled_format;
    texture.texture = texture.memory.CreateTexture(&descriptor);
    if (!texture.texture) {
        return false;
    }
    if (copies) {
        descriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
        descriptor.viewFormatCount = 1;
        texture.copy = device_.handle().CreateTexture(&descriptor);
    }
    wgpu::TextureViewDescriptor view;
    view.format = sampled_format;
    const wgpu::Texture& sampled = copies ? texture.copy : texture.texture;
    texture.group = create_image_group(device_.handle(), layout_, sampled.CreateView(&view), sampler_);
    return true;
}

wgpu::BindGroup VideoTextures::acquire(const wgpu::CommandEncoder& encoder, const VideoFrame& frame,
                                       std::vector<std::uint64_t>& acquired) {
    if (frame.kind == VideoFrame::Kind::cpu) {
        return convert(encoder, frame);
    }
    if (frame.kind == VideoFrame::Kind::webgpu &&
        (frame.device.Get() != device_.handle().Get() || !frame.texture || !frame.texture_lease)) {
        return nullptr;
    }
    auto it = textures_.find(frame.texture_id);
    if (it == textures_.end()) {
        Texture texture;
        if (!import(frame, texture)) {
            if (!warned_) {
                warned_ = true;
                log_warning("video: can't use a frame texture on this WebGPU device (native texture import "
                            "unsupported or incompatible producer device)");
            }
            return nullptr;
        }
        it = textures_.emplace(frame.texture_id, std::move(texture)).first;
    }
    Texture& texture = it->second;
    texture.last_used = frame_;
    texture.owner = frame.owner;
    if (frame.kind == VideoFrame::Kind::webgpu || frame.kind == VideoFrame::Kind::external) {
        if (std::ranges::find(acquired, frame.texture_id) == acquired.end()) {
            acquired.push_back(frame.texture_id);
            ++texture.pending_batches;
        }
        texture.lease = frame.texture_lease;
        return texture.group;
    }
    if (std::ranges::find(acquired, frame.texture_id) == acquired.end()) {
        // Native decoder texture, written outside WebGPU.
        wgpu::SharedTextureMemoryBeginAccessDescriptor access;
        access.initialized = true;
        access.concurrentRead = false;
        wgpu::SharedFence wait;
        if (frame.wait_fence && (wait = fence(frame))) {
            access.fenceCount = 1;
            access.fences = &wait;
            access.signaledValueCount = 1;
            access.signaledValues = &frame.wait_value;
        }
        if (texture.memory.BeginAccess(texture.texture, &access) != wgpu::Status::Success) {
            return nullptr;
        }
        acquired.push_back(frame.texture_id);
        if (const auto owner = texture.owner.lock()) {
            owner->began_reading(frame.texture_id);
        }
        if (texture.copy) {
            wgpu::TexelCopyTextureInfo source;
            source.texture = texture.texture;
            wgpu::TexelCopyTextureInfo destination;
            destination.texture = texture.copy;
            const wgpu::Extent3D size{frame.width, frame.height, 1};
            encoder.CopyTextureToTexture(&source, &destination, &size);
        }
    }
    return texture.group;
}

void VideoTextures::release(const std::vector<std::uint64_t>& acquired) {
    for (const std::uint64_t id : acquired) {
        if (const auto it = textures_.find(id); it != textures_.end()) {
            if (!it->second.memory) {
                // All reads in this batch were submitted. Same-device buffers return to
                // their queue-ordered ring; external allocations stay pinned by other
                // snapshots/current frames, then EndAccess exports their completion fences.
                if (it->second.pending_batches != 0 && --it->second.pending_batches == 0) {
                    it->second.lease.reset();
                }
                continue;
            }
            // A decoder writes a different texture of its ring next, so it doesn't need the end
            // state's fences; an owner that listens gets them, to wait on before writing again.
            wgpu::SharedTextureMemoryEndAccessState state;
            it->second.memory.EndAccess(it->second.texture, &state);
            const auto owner = it->second.owner.lock();
            if (!owner) {
                continue;
            }
            void* handle = nullptr;
            std::uint64_t value = 0;
            for (std::size_t i = 0; i < state.fenceCount && i < state.signaledValueCount; ++i) {
                wgpu::SharedFenceDXGISharedHandleExportInfo shared;
                wgpu::SharedFenceExportInfo info;
                info.nextInChain = &shared;
                state.fences[i].ExportInfo(&info);
                if (info.type == wgpu::SharedFenceType::DXGISharedHandle && shared.handle) {
                    handle = shared.handle;
                    value = state.signaledValues[i];
                }
            }
            owner->finished_reading(id, handle, value);
        }
    }
}

wgpu::SharedFence VideoTextures::fence(const VideoFrame& frame) {
    if (const auto it = fences_.find(frame.wait_fence_id); it != fences_.end()) {
        it->second.last_used = frame_;
        return it->second.fence;
    }
    if (!device_.has_feature(wgpu::FeatureName::SharedFenceDXGISharedHandle)) {
        return nullptr;
    }
    wgpu::SharedFenceDXGISharedHandleDescriptor shared;
    shared.handle = frame.wait_fence;
    wgpu::SharedFenceDescriptor descriptor;
    descriptor.nextInChain = &shared;
    descriptor.label = "shared fence";
    wgpu::SharedFence imported = device_.handle().ImportSharedFence(&descriptor);
    if (imported) {
        fences_.emplace(frame.wait_fence_id, Fence{imported, frame_});
    }
    return imported;
}

void VideoTextures::end_frame() noexcept {
    ++frame_;
    std::erase_if(textures_, [&](const auto& entry) {
        // A pending WebGPU recording owns its reservation until release(), regardless of age.
        return !entry.second.lease &&
               (entry.second.external || frame_ - entry.second.last_used > kIdleFrames);
    });
    std::erase_if(targets_, [&](const auto& entry) { return frame_ - entry.second.last_used > kIdleFrames; });
    std::erase_if(fences_, [&](const auto& entry) { return frame_ - entry.second.last_used > kIdleFrames; });
}

// --- Frames in memory ---

void VideoTextures::create_target(CpuTarget& target, std::uint32_t width, std::uint32_t height) {
    target = {};
    target.width = width;
    target.height = height;
    wgpu::TextureDescriptor descriptor;
    descriptor.label = "video frame";
    descriptor.dimension = wgpu::TextureDimension::e2D;
    descriptor.size = {width, height, 1};
    descriptor.format = kConvertedFormat;
    descriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::RenderAttachment;
    descriptor.viewFormatCount = 1;
    descriptor.viewFormats = &kSampledFormat;
    target.texture = device_.handle().CreateTexture(&descriptor);
    target.render_view = target.texture.CreateView();
    wgpu::TextureViewDescriptor sample_view;
    sample_view.format = kSampledFormat;
    target.sample_group =
        create_image_group(device_.handle(), layout_, target.texture.CreateView(&sample_view), sampler_);
    target.pixels = std::make_unique<Buffer>(device_.handle(), wgpu::BufferUsage::Storage);
}

wgpu::BindGroup VideoTextures::convert(const wgpu::CommandEncoder& encoder, const VideoFrame& frame) {
    if (!frame.pixels || frame.width == 0 || frame.height == 0 ||
        frame.pixels->size() < packed_size(frame.format, frame.width, frame.height)) {
        return nullptr;
    }
    CpuTarget& target = targets_[frame.source_id];
    target.last_used = frame_;
    if (target.width != frame.width || target.height != frame.height) {
        create_target(target, frame.width, frame.height); // the old image lives on while in use
        target.last_used = frame_;
    }
    if (target.converted && target.frame_id == frame.frame_id) {
        return target.sample_group; // converted already (another window this frame)
    }

    target.pixels->write(std::as_bytes(std::span(*frame.pixels)));
    if (target.bound != target.pixels->handle().Get()) {
        wgpu::BindGroupEntry entry;
        entry.binding = 0;
        entry.buffer = target.pixels->handle();
        wgpu::BindGroupDescriptor group;
        group.layout = frame_layout_;
        group.entryCount = 1;
        group.entries = &entry;
        target.frame_group = device_.handle().CreateBindGroup(&group);
        target.bound = target.pixels->handle().Get();
    }

    wgpu::RenderPassColorAttachment color;
    color.view = target.render_view;
    color.loadOp = wgpu::LoadOp::Clear; // every pixel is written
    color.storeOp = wgpu::StoreOp::Store;
    wgpu::RenderPassDescriptor descriptor;
    descriptor.label = "video convert";
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    const wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&descriptor);
    pass.SetPipeline(convert_pipeline_);
    pass.SetBindGroup(0, target.frame_group);
    const ConvertImmediates immediates{static_cast<std::uint32_t>(frame.format), frame.width, frame.height,
                                       static_cast<std::uint32_t>(frame.matrix), frame.full_range ? 1u : 0u};
    pass.SetImmediates(0, &immediates, sizeof(immediates));
    pass.Draw(3);
    pass.End();
    target.converted = true;
    target.frame_id = frame.frame_id;
    return target.sample_group;
}

} // namespace elg::detail::gpu
