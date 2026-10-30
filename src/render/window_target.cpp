// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render/window_target.h"

#include "render/color_space.h"

#if defined(_WIN32)
#include "core/log.h"
#include "platform/generic/platform.h"

#include <atomic>
#endif

#include <algorithm>
#include <cstdint>
#include <span>

namespace elg::detail {

namespace {

#if defined(_WIN32)
// The longest a composition frame waits for the display to be ready (it's a hint for pacing).
constexpr unsigned kReadyTimeoutMs = 50;

// Whether a window presents through DirectComposition (platform/windows/composition.h) instead of its
// WebGPU surface.
bool wants_composition(const gpu::Device& device, bool transparent, void* native_window) {
    if (!native_window) {
        return false;
    }
    // Before composition_allowed(), whose log line would otherwise claim DirectComposition.
    if (!gpu::CompositionTarget::supported(device)) {
        static std::atomic_flag logged;
        if (!logged.test_and_set()) {
            log_info("windows present through WebGPU surfaces (the GPU backend can't share textures with "
                     "DirectComposition)");
        }
        return false;
    }
    if (!gpu::composition_allowed()) {
        return false;
    }
    // ELG_PRESENT=surface: WebGPU surfaces for opaque windows only.
    return transparent || platform::environment_variable("ELG_PRESENT").value_or("") != "surface";
}
#endif

// Layers hold premultiplied linear color; sRGB storage keeps 8-bit precision where it matters.
constexpr wgpu::TextureFormat kLayerFormat = wgpu::TextureFormat::RGBA8UnormSrgb;
static_assert(kLayerFormat == gpu::BlurRenderer::kColorFormat);
// Layers can be copied from, for backdrops read inside them.
constexpr wgpu::TextureUsage kLayerUsage =
    wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc;

std::uint32_t round_up(std::uint32_t value, std::uint32_t multiple) {
    return (value + multiple - 1) / multiple * multiple;
}

// Clears the target first, or with no `clear` continues on what it holds.
wgpu::RenderPassEncoder begin_pass(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& view,
                                   const std::optional<wgpu::Color>& clear, const char* label) {
    wgpu::RenderPassColorAttachment color;
    color.view = view;
    color.loadOp = clear ? wgpu::LoadOp::Clear : wgpu::LoadOp::Load;
    color.storeOp = wgpu::StoreOp::Store;
    color.clearValue = clear.value_or(wgpu::Color{});
    wgpu::RenderPassDescriptor descriptor;
    descriptor.label = label;
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    return encoder.BeginRenderPass(&descriptor);
}

// Brackets rendering into an offscreen image that is sampled afterwards.
template <class Draw>
void render_offscreen(const wgpu::CommandEncoder& encoder, const gpu::RenderImage& target, const char* label,
                      Draw&& draw) {
    const wgpu::RenderPassEncoder pass = begin_pass(encoder, target.view(), wgpu::Color{0, 0, 0, 0}, label);
    draw(pass);
    pass.End();
}

} // namespace

WindowTarget::WindowTarget(const gpu::Device& device, wgpu::Surface surface, bool transparent, void* native_window)
    : device_(device), sampler_(gpu::create_linear_sampler(device.handle())),
      instances_(device.handle(), wgpu::BufferUsage::Vertex), stops_(device.handle(), wgpu::BufferUsage::Storage),
      path_vertices_(device.handle(), wgpu::BufferUsage::Vertex) {
#if defined(_WIN32)
    // Every window presents through DirectComposition (no stretched frames while resizing, and
    // transparency), unless wants_composition() says otherwise.
    if (wants_composition(device, transparent, native_window)) {
        try {
            composition_ = std::make_unique<gpu::CompositionTarget>(device, native_window, transparent);
        } catch (const std::exception& error) {
            log_warning("window falls back to a WebGPU surface: {}", error.what());
        }
    }
    if (transparent && !composition_) {
        static std::atomic_flag warned;
        if (!warned.test_and_set()) {
            log_warning("transparent windows render opaque: they present through WebGPU surfaces, which "
                        "can't be transparent on Windows");
        }
    }
    if (composition_) {
        return;
    }
#else
    (void)native_window;
#endif
    swapchain_ = std::make_unique<gpu::Swapchain>(device, std::move(surface), transparent);
}

void WindowTarget::render(const WindowFrame& frame, const RenderResources& resources) {
#if defined(_WIN32)
    if (composition_) {
        render_composition(frame, resources);
        return;
    }
#endif
    const auto width = static_cast<std::uint32_t>(std::max(frame.pixel_width, 0));
    const auto height = static_cast<std::uint32_t>(std::max(frame.pixel_height, 0));
    if (!swapchain_->prepare(width, height)) {
        return; // minimized
    }
    wgpu::TextureView view = swapchain_->acquire();
    if (!view) {
        // Out of date: reconfigured, then once more; still resizing if that fails too.
        if (!swapchain_->prepare(width, height) || !(view = swapchain_->acquire())) {
            return;
        }
    }

    std::vector<std::uint64_t> acquired_videos;
    const wgpu::CommandBuffer commands = record({view, swapchain_->view_format(), swapchain_->width(),
                                                 swapchain_->height(), swapchain_->copy_source()},
                                                frame, resources, acquired_videos);
    device_.submit(commands);
    resources.videos.release(acquired_videos); // back to the decoder
    swapchain_->present();
    if (window_reads_backdrops_) {
        // From the next frame on, backdrops come straight from the window's images (when the
        // surface allows): no offscreen window, no full-window copy.
        swapchain_->request_copy_source();
    }
    device_.tick();
}

void WindowTarget::render_composition([[maybe_unused]] const WindowFrame& frame,
                                      [[maybe_unused]] const RenderResources& resources) {
#if defined(_WIN32)
    const auto width = static_cast<std::uint32_t>(std::max(frame.pixel_width, 0));
    const auto height = static_cast<std::uint32_t>(std::max(frame.pixel_height, 0));
    if (!composition_->prepare(width, height)) {
        return; // minimized
    }
    composition_->wait_until_ready(kReadyTimeoutMs); // paced to the display
    const Color clear = frame.list.clear_color;
    composition_->set_background({clear.r, clear.g, clear.b, 1});

    const wgpu::TextureView view = composition_->begin_access();
    std::vector<std::uint64_t> acquired_videos;
    const wgpu::CommandBuffer commands = record({view, composition_->view_format(), composition_->width(),
                                                 composition_->height()},
                                                frame, resources, acquired_videos);
    device_.submit(commands);
    resources.videos.release(acquired_videos);
    composition_->end_access();
    // Direct3D copies the image next, so the rendering must be finished.
    device_.wait_idle();
    composition_->present();
    device_.tick();
#endif
}

const WindowTarget::CachedMask* WindowTarget::cached_mask(std::uint64_t key) {
    const auto it = masks_.find(key);
    if (it == masks_.end()) {
        return nullptr;
    }
    it->second.last_used = frame_number_;
    return &it->second;
}

// Drops masks nobody drew lately: past the budget the least recently used go first, otherwise
// masks unused for a few seconds' worth of frames. (WebGPU keeps a mask alive for frames still
// on the GPU, so dropping one is always safe.)
void WindowTarget::evict_masks() {
    constexpr std::size_t kBudget = 32u << 20; // bytes of R8 coverage
    constexpr std::uint64_t kIdleFrames = 240; // frames drawn without the mask
    for (auto it = masks_.begin(); it != masks_.end();) {
        if (it->second.last_used + kIdleFrames < frame_number_) {
            mask_bytes_ -= std::size_t{it->second.mask.width()} * it->second.mask.height();
            it = masks_.erase(it);
        } else {
            ++it;
        }
    }
    while (mask_bytes_ > kBudget) {
        auto oldest = masks_.end();
        for (auto it = masks_.begin(); it != masks_.end(); ++it) {
            if (it->second.last_used < frame_number_ &&
                (oldest == masks_.end() || it->second.last_used < oldest->second.last_used)) {
                oldest = it;
            }
        }
        if (oldest == masks_.end()) {
            break; // everything is in use by this frame
        }
        mask_bytes_ -= std::size_t{oldest->second.mask.width()} * oldest->second.mask.height();
        masks_.erase(oldest);
    }
}

wgpu::BindGroup WindowTarget::image_group(const RenderResources& resources, const wgpu::TextureView& view) const {
    return gpu::create_image_group(device_.handle(), resources.image_layout, view, sampler_);
}

gpu::RenderImage& WindowTarget::pooled_image(std::vector<gpu::RenderImage>& pool, std::size_t index,
                                             wgpu::TextureFormat format, std::uint32_t width, std::uint32_t height,
                                             std::uint32_t samples, wgpu::TextureUsage usage) {
    // Sizes are rounded up so small changes (animations, resizes) reuse the same image.
    if (index < pool.size() && pool[index].width() >= width && pool[index].height() >= height) {
        return pool[index];
    }
    std::uint32_t capacity_width = round_up(width, 256);
    std::uint32_t capacity_height = round_up(height, 256);
    if (index < pool.size()) {
        capacity_width = std::max(capacity_width, pool[index].width());
        capacity_height = std::max(capacity_height, pool[index].height());
        pool[index] = gpu::RenderImage(device_.handle(), format, capacity_width, capacity_height, samples, usage);
        return pool[index];
    }
    return pool.emplace_back(device_.handle(), format, capacity_width, capacity_height, samples, usage);
}

const wgpu::BindGroup& WindowTarget::empty_group(const RenderResources& resources) {
    if (!empty_group_) {
        empty_ = gpu::RenderImage(device_.handle(), kLayerFormat, 1, 1); // zeroed: transparent
        empty_group_ = image_group(resources, empty_->view());
    }
    return empty_group_;
}

wgpu::BindGroup WindowTarget::blur_color(const wgpu::CommandEncoder& encoder, const wgpu::BindGroup& source,
                                         std::uint32_t width, std::uint32_t height, float sigma, std::uint32_t factor,
                                         bool clamp_edges, std::size_t slot, const RenderResources& resources) {
    wgpu::BindGroup input = source;
    if (factor > 1) {
        width = (width + factor - 1) / factor;
        height = (height + factor - 1) / factor;
        const gpu::RenderImage& down = pooled_image(color_down_, slot, kLayerFormat, width, height);
        render_offscreen(encoder, down, "downsample", [&](const wgpu::RenderPassEncoder& pass) {
            resources.blur.downsample(pass, source, width * factor, height * factor, factor, clamp_edges);
        });
        input = image_group(resources, down.view());
        sigma /= static_cast<float>(factor);
    }
    const gpu::RenderImage& horizontal = pooled_image(color_horizontal_, slot, kLayerFormat, width, height);
    render_offscreen(encoder, horizontal, "blur", [&](const wgpu::RenderPassEncoder& pass) {
        resources.blur.blur(pass, input, width, height, true, sigma, gpu::BlurRenderer::Channel::color, clamp_edges);
    });
    const wgpu::BindGroup horizontal_group = image_group(resources, horizontal.view());
    const gpu::RenderImage& vertical = pooled_image(color_vertical_, slot, kLayerFormat, width, height);
    render_offscreen(encoder, vertical, "blur", [&](const wgpu::RenderPassEncoder& pass) {
        resources.blur.blur(pass, horizontal_group, width, height, false, sigma, gpu::BlurRenderer::Channel::color,
                            clamp_edges);
    });
    return image_group(resources, vertical.view());
}

wgpu::BindGroup WindowTarget::take_backdrop(const wgpu::CommandEncoder& encoder, const wgpu::Texture& texture,
                                            const PlanBackdrop& backdrop, std::size_t slot,
                                            const RenderResources& resources) {
    // One set of images serves every backdrop: each is read by the pass right after it, before
    // the next is taken (WebGPU keeps the order). A copy's format must match the source's, up
    // to sRGB: layers are kLayerFormat, the window's surface image may differ (e.g. BGRA).
    const wgpu::TextureFormat format = gpu::srgb_variant(texture.GetFormat());
    const gpu::RenderImage& copy =
        pooled_image(format == kLayerFormat ? backdrop_copies_ : window_copies_, 0, format, backdrop.width,
                     backdrop.height, 1, wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst);
    wgpu::TexelCopyTextureInfo source;
    source.texture = texture;
    source.origin = {backdrop.x, backdrop.y, 0};
    wgpu::TexelCopyTextureInfo destination;
    destination.texture = copy.texture();
    const wgpu::Extent3D size{backdrop.width, backdrop.height, 1};
    encoder.CopyTextureToTexture(&source, &destination, &size);
    const wgpu::BindGroup group = image_group(resources, copy.view());
    if (backdrop.sigma <= 0) {
        return group;
    }
    return blur_color(encoder, group, backdrop.width, backdrop.height, backdrop.sigma, backdrop.downsample, true,
                      slot, resources);
}

void WindowTarget::draw_pass(const wgpu::CommandEncoder& encoder, const PassTarget& target, const PlanPass& plan_pass,
                             const FramePlan& plan, const FrameGroups& groups, const RenderResources& resources) {
    gpu::QuadRenderer& quads = resources.quads;
    wgpu::BindGroup second = empty_group(resources);
    wgpu::RenderPassEncoder pass;
    const auto begin = [&](bool clear) {
        pass = begin_pass(encoder, target.view, clear ? std::optional(target.clear) : std::nullopt, "pass");
        quads.begin(pass, target.format, target.width, target.height, instances_.handle(), resources.atlas,
                    groups.stops, second);
    };
    begin(true);
    for (const PlanSegment& segment : plan_pass.segments) {
        if (segment.backdrop >= 0 && target.texture) {
            // What's been drawn so far, copied out between two passes.
            pass.End();
            // Its blur uses the pooled images after the blurred layers' (which live until composited).
            second = take_backdrop(encoder, target.texture, plan.backdrops[static_cast<std::size_t>(segment.backdrop)],
                                   plan.layers.size(), resources);
            begin(false);
        }

        wgpu::BindGroup image;
        if (segment.video >= 0) {
            image = groups.videos[static_cast<std::size_t>(segment.video)];
            if (!image) {
                continue; // no frame yet: nothing drawn
            }
        } else if (segment.image >= 0) {
            image = groups.images[static_cast<std::size_t>(segment.image)];
        } else if (segment.path >= 0) {
            image = groups.paths[static_cast<std::size_t>(segment.path)];
        } else if (segment.layer >= 0) {
            const LayerGroups& layer = groups.layers[static_cast<std::size_t>(segment.layer)];
            switch (segment.layer_image) {
            case LayerImage::content: image = layer.content; break;
            case LayerImage::shadow: image = layer.shadow; break;
            case LayerImage::inner_shadow:
                image = layer.inner_shadow;
                quads.bind_second_image(pass, layer.raw);
                break;
            }
            if (!image) {
                continue;
            }
        }

        if (segment.mask) {
            // Masks what the pass has drawn so far (a layer clipped to a path, or View::mask).
            quads.set_masking(pass, target.format, true);
        }
        if (image) {
            quads.bind_image(pass, image);
        }
        quads.draw(pass, segment.first, segment.count);
        if (image) {
            quads.bind_image(pass, resources.atlas);
        }
        if (segment.mask) {
            quads.set_masking(pass, target.format, false);
        }
        if (segment.layer_image == LayerImage::inner_shadow) {
            quads.bind_second_image(pass, second);
        }
    }
    pass.End();
}

wgpu::CommandBuffer WindowTarget::record(const DrawTarget& target, const WindowFrame& frame,
                                         const RenderResources& resources,
                                         std::vector<std::uint64_t>& acquired_videos) {
    const FramePlan plan = plan_frame(frame.list, target.width, target.height);
    instances_.write(std::as_bytes(std::span(plan.instances)));
    stops_.write(std::as_bytes(std::span(plan.stops)));
    if (!plan.path_vertices.empty()) {
        path_vertices_.write(std::as_bytes(std::span(plan.path_vertices)));
    }

    const wgpu::CommandEncoder encoder = device_.handle().CreateCommandEncoder();
    FrameGroups groups;
    // Gradient stops and color matrices for every pass.
    groups.stops = resources.quads.stops_group(stops_.handle());

    // Path coverage masks first: any pass may fill through them. Masks with the same content as
    // in an earlier frame are reused; new ones render into the cache.
    ++frame_number_;
    groups.paths.resize(plan.paths.size());
    std::size_t rendered = 0;
    for (std::size_t i = 0; i < plan.paths.size(); ++i) {
        const PlanPath& path = plan.paths[i];
        const CachedMask* mask = cached_mask(path.key);
        if (!mask) {
            gpu::PathRenderer& paths = resources.paths;
            const gpu::RenderImage& samples =
                pooled_image(path_samples_, rendered, gpu::PathRenderer::kMaskFormat, path.width, path.height,
                             gpu::PathRenderer::kSamples);
            const gpu::RenderImage& stencil =
                pooled_image(path_stencils_, rendered, gpu::PathRenderer::kStencilFormat, path.width, path.height,
                             gpu::PathRenderer::kSamples, wgpu::TextureUsage::RenderAttachment);
            ++rendered;
            gpu::RenderImage image(device_.handle(), gpu::PathRenderer::kMaskFormat, path.width, path.height);
            paths.render(encoder, samples, stencil, image, path.width, path.height, path_vertices_.handle(),
                         path.first_vertex, path.vertex_count, path.even_odd);
            wgpu::BindGroup group = image_group(resources, image.view());
            mask = &masks_.insert_or_assign(path.key, CachedMask{std::move(image), std::move(group), frame_number_})
                        .first->second;
            mask_bytes_ += std::size_t{path.width} * path.height;
        }
        groups.paths[i] = mask->group;
    }
    evict_masks();

    // Images: uploaded (with their mips) the first time a frame draws them.
    groups.images.reserve(plan.images.size());
    for (const auto& image : plan.images) {
        groups.images.push_back(resources.textures.acquire(encoder, *image));
    }
    resources.textures.end_frame();

    // Sample the same snapshots that determined the picture geometry.
    for (const auto& video_frame : plan.video_frames) {
        groups.videos.push_back(video_frame ? resources.videos.acquire(encoder, *video_frame, acquired_videos)
                                            : wgpu::BindGroup());
    }
    resources.videos.end_frame();

    // Offscreen layers first, innermost first; each may also be blurred, and get a two-pass
    // blurred shadow and inner shadow.
    groups.layers.resize(plan.layers.size());
    std::size_t shadows = 0;
    std::size_t inner_shadows = 0;
    for (std::size_t i = 0; i < plan.layers.size(); ++i) {
        const PlanPass& layer = plan.layers[i];
        LayerGroups& layer_groups = groups.layers[i];

        const gpu::RenderImage& content =
            pooled_image(layers_, i, kLayerFormat, layer.width, layer.height, 1, kLayerUsage);
        draw_pass(encoder, {content.texture(), content.view(), kLayerFormat, layer.width, layer.height, {0, 0, 0, 0}},
                  layer, plan, groups, resources);
        layer_groups.raw = image_group(resources, content.view());
        layer_groups.content = layer_groups.raw;

        if (layer.shadow_sigma) {
            const float sigma = *layer.shadow_sigma;
            const gpu::RenderImage& horizontal =
                pooled_image(blur_horizontal_, shadows, gpu::BlurRenderer::kFormat, layer.width, layer.height);
            render_offscreen(encoder, horizontal, "layer shadow", [&](const wgpu::RenderPassEncoder& pass) {
                resources.blur.blur(pass, layer_groups.raw, layer.width, layer.height, true, sigma,
                                    gpu::BlurRenderer::Channel::alpha);
            });
            const wgpu::BindGroup horizontal_group = image_group(resources, horizontal.view());

            const gpu::RenderImage& vertical =
                pooled_image(blur_vertical_, shadows, gpu::BlurRenderer::kFormat, layer.width, layer.height);
            render_offscreen(encoder, vertical, "layer shadow", [&](const wgpu::RenderPassEncoder& pass) {
                resources.blur.blur(pass, horizontal_group, layer.width, layer.height, false, sigma,
                                    gpu::BlurRenderer::Channel::red);
            });
            layer_groups.shadow = image_group(resources, vertical.view());
            ++shadows;
        }
        if (layer.inner_shadow_sigma) {
            // The blurred inverse of the alpha: full outside the content, fading in from its edges.
            const float sigma = *layer.inner_shadow_sigma;
            const gpu::RenderImage& horizontal =
                pooled_image(inner_horizontal_, inner_shadows, gpu::BlurRenderer::kFormat, layer.width, layer.height);
            render_offscreen(encoder, horizontal, "inner shadow", [&](const wgpu::RenderPassEncoder& pass) {
                resources.blur.blur(pass, layer_groups.raw, layer.width, layer.height, true, sigma,
                                    gpu::BlurRenderer::Channel::inverse_alpha);
            });
            const wgpu::BindGroup horizontal_group = image_group(resources, horizontal.view());
            const gpu::RenderImage& vertical =
                pooled_image(inner_vertical_, inner_shadows, gpu::BlurRenderer::kFormat, layer.width, layer.height);
            render_offscreen(encoder, vertical, "inner shadow", [&](const wgpu::RenderPassEncoder& pass) {
                resources.blur.blur(pass, horizontal_group, layer.width, layer.height, false, sigma,
                                    gpu::BlurRenderer::Channel::red, true);
            });
            layer_groups.inner_shadow = image_group(resources, vertical.view());
            ++inner_shadows;
        }
        if (layer.blur_sigma) {
            layer_groups.content = blur_color(encoder, layer_groups.raw, layer.width, layer.height, *layer.blur_sigma,
                                              layer.blur_downsample, layer.blur_clamp, i, resources);
        }
    }

    // The window itself. Its previous contents are discarded (cleared). When it reads backdrops
    // and its image can't be copied from, it draws offscreen, then onto the window.
    const auto clear = to_linear_premultiplied(frame.list.clear_color);
    const wgpu::Color clear_color{static_cast<double>(clear[0]), static_cast<double>(clear[1]),
                                          static_cast<double>(clear[2]), static_cast<double>(clear[3])};
    window_reads_backdrops_ = plan.blit_instance.has_value();
    if (plan.blit_instance && !target.texture) {
        const gpu::RenderImage& image =
            pooled_image(frame_images_, 0, kLayerFormat, target.width, target.height, 1, kLayerUsage);
        draw_pass(encoder, {image.texture(), image.view(), kLayerFormat, target.width, target.height, clear_color},
                  plan.main, plan, groups, resources);
        const wgpu::RenderPassEncoder pass = begin_pass(encoder, target.view, clear_color, "window");
        resources.quads.begin(pass, target.format, target.width, target.height, instances_.handle(),
                              image_group(resources, image.view()), groups.stops, empty_group(resources));
        resources.quads.draw(pass, *plan.blit_instance, 1);
        pass.End();
    } else {
        if (target.texture) {
            frame_images_.clear(); // drawn directly from now on
        }
        draw_pass(encoder, {target.texture, target.view, target.format, target.width, target.height, clear_color},
                  plan.main, plan, groups, resources);
    }
    return encoder.Finish();
}

} // namespace elg::detail
