// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render/window_target.h"

#include "core/crash.h"
#include "gpu/blur_renderer.h"
#include "gpu/buffer.h"
#include "gpu/device.h"
#include "gpu/path_renderer.h"
#include "gpu/presenter.h"
#include "gpu/quad_renderer.h"
#include "gpu/render_image.h"
#include "gpu/texture_cache.h"
#include "gpu/webgpu.h"
#include "render/color_space.h"
#include "render/frame_plan.h"
#include "render/intermediate_images.h"
#include "render/video_textures.h"

#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace elg::detail {

namespace {

// Layers hold premultiplied linear color; sRGB storage keeps 8-bit precision where it matters.
// Color blurs (gpu::BlurRenderer::kColorFormat) read and write it too.
constexpr wgpu::TextureFormat kLayerFormat = wgpu::TextureFormat::RGBA8UnormSrgb;
// Layers can be copied from, for backdrops read inside them.
constexpr wgpu::TextureUsage kLayerUsage =
    wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopySrc;
// The color blur images backdrops share (blurred layers use their layer's index).
constexpr std::uint64_t kBackdropBlurSlot = ~std::uint64_t{0};

// An offscreen image that is rendered to and then sampled (layers, blur passes, path masks), or
// a multisampled / stencil scratch attachment (path masks): its texture (gpu/render_image.h)
// and the view passes use.
class RenderImage {
public:
    RenderImage(const wgpu::Device& device, wgpu::TextureFormat format, std::uint32_t width, std::uint32_t height,
                std::uint32_t samples = 1,
                wgpu::TextureUsage usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding)
        : format_(format), width_(width), height_(height),
          texture_(gpu::create_render_texture(device, format, width, height, samples, usage)),
          view_(texture_.CreateView()) {}

    [[nodiscard]] const wgpu::Texture& texture() const noexcept { return texture_; }
    [[nodiscard]] const wgpu::TextureView& view() const noexcept { return view_; }
    [[nodiscard]] wgpu::TextureFormat format() const noexcept { return format_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

    wgpu::BindGroup group; // path masks: the group that samples it

private:
    wgpu::TextureFormat format_;
    std::uint32_t width_;
    std::uint32_t height_;
    wgpu::Texture texture_;
    wgpu::TextureView view_;
};

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
void render_offscreen(const wgpu::CommandEncoder& encoder, const RenderImage& target, const char* label,
                      const Draw& draw) {
    const wgpu::RenderPassEncoder pass = begin_pass(encoder, target.view(), wgpu::Color{0, 0, 0, 0}, label);
    draw(pass);
    pass.End();
}

} // namespace

class WindowTarget::State {
public:
    State(const gpu::Device& device, const gpu::SurfaceSource& surface, bool transparent, void* native_window);

    void render(const WindowFrame& frame, const RenderResources& resources);

private:
    struct LayerGroups {
        wgpu::BindGroup content; // blurred, when the layer blurs
        wgpu::BindGroup raw;     // as drawn
        wgpu::BindGroup shadow;
        wgpu::BindGroup inner_shadow;
    };

    // Where a frame goes: the presenter's image. `texture` is set when it can be copied from:
    // backdrops are then read from it directly, instead of the window drawing offscreen and
    // being copied over.
    struct DrawTarget {
        wgpu::TextureView view;
        wgpu::TextureFormat format{};
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        wgpu::Texture texture;
    };

    // What a pass draws into. `texture` is set when it can be copied from (layers, the offscreen
    // window image): only such passes can read backdrops.
    struct PassTarget {
        wgpu::Texture texture;
        wgpu::TextureView view;
        wgpu::TextureFormat format{};
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        wgpu::Color clear;
    };

    // The images and groups a frame's passes sample.
    struct FrameGroups {
        wgpu::BindGroup stops;
        std::vector<LayerGroups> layers;
        std::vector<wgpu::BindGroup> paths;
        std::vector<wgpu::BindGroup> images;
        std::vector<wgpu::BindGroup> videos;
    };

    // Records the frame. `acquired_videos` collects the shared video textures it samples, to
    // release once it's submitted.
    wgpu::CommandBuffer record(const DrawTarget& target, const WindowFrame& frame, const RenderResources& resources,
                               std::vector<std::uint64_t>& acquired_videos);
    // Records a pass, split wherever its instances read a backdrop.
    void draw_pass(const wgpu::CommandEncoder& encoder, const PassTarget& target, const PlanPass& plan_pass,
                   const FramePlan& plan, const FrameGroups& groups, const RenderResources& resources);
    // Copies a backdrop's region out of `texture` and blurs it as planned; returns its group.
    wgpu::BindGroup take_backdrop(const wgpu::CommandEncoder& encoder, const wgpu::Texture& texture,
                                  const PlanBackdrop& backdrop, const RenderResources& resources);
    // Blurs a color image (`width` x `height` of it) into pooled images, downsampled by `factor`
    // first; `slot` picks the pooled images (ImageSlot::index). Returns the result's group.
    wgpu::BindGroup blur_color(const wgpu::CommandEncoder& encoder, const wgpu::BindGroup& source, std::uint32_t width,
                               std::uint32_t height, float sigma, std::uint32_t factor, bool clamp_edges,
                               std::uint64_t slot, const RenderResources& resources);
    const wgpu::BindGroup& empty_group(const RenderResources& resources);
    // A side of the window's image: the window's (in pixels), up to what the device's textures
    // allow.
    [[nodiscard]] std::uint32_t window_side(int pixels) const;
    [[nodiscard]] wgpu::BindGroup image_group(const RenderResources& resources, const wgpu::TextureView& view) const;
    RenderImage& pooled_image(ImageRole role, std::uint64_t index, wgpu::TextureFormat format, std::uint32_t width,
                              std::uint32_t height, std::uint32_t samples = 1,
                              wgpu::TextureUsage usage = wgpu::TextureUsage::RenderAttachment |
                                                         wgpu::TextureUsage::TextureBinding);

    const gpu::Device& device_;
    // Every offscreen image, path coverage masks included: masks are kept by content
    // (PlanPath::key), so unchanged paths are only composited, not rendered again.
    IntermediateImages<RenderImage> images_;
    PathHashes path_hashes_;
    std::vector<std::array<float, 2>> path_fans_; // of the masks a frame renders
    std::unique_ptr<gpu::Presenter> presenter_;
    wgpu::Sampler sampler_; // for sampling layers
    gpu::Buffer instances_;
    gpu::Buffer stops_;
    gpu::Buffer path_vertices_;
    bool window_reads_backdrops_ = false; // the last frame recorded did
    std::optional<RenderImage> empty_;    // bound where no second image is
    wgpu::BindGroup empty_group_;
};

WindowTarget::WindowTarget(const gpu::Device& device, const gpu::SurfaceSource& surface, bool transparent,
                           void* native_window)
    : state_(std::make_unique<State>(device, surface, transparent, native_window)) {}

WindowTarget::~WindowTarget() = default;

void WindowTarget::render(const WindowFrame& frame, const RenderResources& resources) {
    state_->render(frame, resources);
}

WindowTarget::State::State(const gpu::Device& device, const gpu::SurfaceSource& surface, bool transparent,
                           void* native_window)
    : device_(device), presenter_(gpu::create_presenter(device, surface, transparent, native_window)),
      sampler_(gpu::create_linear_sampler(device.handle())), instances_(device.handle(), wgpu::BufferUsage::Vertex),
      stops_(device.handle(), wgpu::BufferUsage::Storage), path_vertices_(device.handle(), wgpu::BufferUsage::Vertex) {
    ELG_DEBUG_ASSERT(kLayerFormat == gpu::BlurRenderer::kColorFormat, "layers and color blurs differ in format");
}

void WindowTarget::State::render(const WindowFrame& frame, const RenderResources& resources) {
    const Color clear = frame.list.clear_color;
    const wgpu::TextureView view = presenter_->begin_frame(
        window_side(frame.pixel_width), window_side(frame.pixel_height), {clear.r, clear.g, clear.b, 1});
    if (!view) {
        return; // minimized, or skipped
    }

    std::vector<std::uint64_t> acquired_videos;
    const wgpu::CommandBuffer commands = record({view, presenter_->view_format(), presenter_->width(),
                                                 presenter_->height(), presenter_->copy_source()},
                                                frame, resources, acquired_videos);
    device_.submit(commands);
    resources.videos.release(acquired_videos); // back to the decoder
    presenter_->end_frame(window_reads_backdrops_);
    device_.tick();
}

std::uint32_t WindowTarget::State::window_side(int pixels) const {
    return std::min(static_cast<std::uint32_t>(std::max(pixels, 0)), device_.max_texture_side());
}

wgpu::BindGroup WindowTarget::State::image_group(const RenderResources& resources,
                                                 const wgpu::TextureView& view) const {
    return gpu::create_image_group(device_.handle(), resources.image_layout, view, sampler_);
}

RenderImage& WindowTarget::State::pooled_image(ImageRole role, std::uint64_t index, wgpu::TextureFormat format,
                                               std::uint32_t width, std::uint32_t height, std::uint32_t samples,
                                               wgpu::TextureUsage usage) {
    return images_.acquire({role, index}, format, width, height, samples, device_.max_texture_side(),
                           [&](std::uint32_t w, std::uint32_t h) {
                               return RenderImage(device_.handle(), format, w, h, samples, usage);
                           });
}

const wgpu::BindGroup& WindowTarget::State::empty_group(const RenderResources& resources) {
    if (!empty_group_) {
        empty_ = RenderImage(device_.handle(), kLayerFormat, 1, 1); // zeroed: transparent
        empty_group_ = image_group(resources, empty_->view());
    }
    return empty_group_;
}

wgpu::BindGroup WindowTarget::State::blur_color(const wgpu::CommandEncoder& encoder, const wgpu::BindGroup& source,
                                                std::uint32_t width, std::uint32_t height, float sigma,
                                                std::uint32_t factor, bool clamp_edges, std::uint64_t slot,
                                                const RenderResources& resources) {
    wgpu::BindGroup input = source;
    if (factor > 1) {
        width = (width + factor - 1) / factor;
        height = (height + factor - 1) / factor;
        const RenderImage& down = pooled_image(ImageRole::color_down, slot, kLayerFormat, width, height);
        render_offscreen(encoder, down, "downsample", [&](const wgpu::RenderPassEncoder& pass) {
            resources.blur.downsample(pass, source, width * factor, height * factor, factor, clamp_edges);
        });
        input = image_group(resources, down.view());
        sigma /= static_cast<float>(factor);
    }
    const RenderImage& horizontal = pooled_image(ImageRole::color_horizontal, slot, kLayerFormat, width, height);
    render_offscreen(encoder, horizontal, "blur", [&](const wgpu::RenderPassEncoder& pass) {
        resources.blur.blur(pass, input, width, height, true, sigma, gpu::BlurRenderer::Channel::color, clamp_edges);
    });
    const wgpu::BindGroup horizontal_group = image_group(resources, horizontal.view());
    const RenderImage& vertical = pooled_image(ImageRole::color_blurred, slot, kLayerFormat, width, height);
    render_offscreen(encoder, vertical, "blur", [&](const wgpu::RenderPassEncoder& pass) {
        resources.blur.blur(pass, horizontal_group, width, height, false, sigma, gpu::BlurRenderer::Channel::color,
                            clamp_edges);
    });
    return image_group(resources, vertical.view());
}

wgpu::BindGroup WindowTarget::State::take_backdrop(const wgpu::CommandEncoder& encoder, const wgpu::Texture& texture,
                                                   const PlanBackdrop& backdrop, const RenderResources& resources) {
    // One set of images serves every backdrop: each is read by the pass right after it, before
    // the next is taken (WebGPU keeps the order). A copy's format must match the source's, up
    // to sRGB: layers are kLayerFormat, the window's surface image may differ (e.g. BGRA).
    const wgpu::TextureFormat format = gpu::srgb_variant(texture.GetFormat());
    const RenderImage& copy =
        pooled_image(format == kLayerFormat ? ImageRole::backdrop_copy : ImageRole::window_copy, 0, format,
                     backdrop.width, backdrop.height, 1,
                     wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst);
    wgpu::TexelCopyTextureInfo source;
    source.texture = texture;
    source.origin = {backdrop.x, backdrop.y, 0};
    wgpu::TexelCopyTextureInfo destination;
    destination.texture = copy.texture();
    const wgpu::Extent3D size{backdrop.width, backdrop.height, 1};
    encoder.CopyTextureToTexture(&source, &destination, &size);
    if (backdrop.sigma <= 0) {
        return image_group(resources, copy.view());
    }
    return blur_color(encoder, image_group(resources, copy.view()), backdrop.width, backdrop.height, backdrop.sigma,
                      backdrop.downsample, true, kBackdropBlurSlot, resources);
}

void WindowTarget::State::draw_pass(const wgpu::CommandEncoder& encoder, const PassTarget& target,
                                    const PlanPass& plan_pass, const FramePlan& plan, const FrameGroups& groups,
                                    const RenderResources& resources) {
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
            // Its blur has pooled images of its own: the blurred layers' live until composited.
            second = take_backdrop(encoder, target.texture, plan.backdrops[static_cast<std::size_t>(segment.backdrop)],
                                   resources);
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
            if (!image) {
                continue; // no texture (larger than the device allows): nothing drawn
            }
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

wgpu::CommandBuffer WindowTarget::State::record(const DrawTarget& target, const WindowFrame& frame,
                                                const RenderResources& resources,
                                                std::vector<std::uint64_t>& acquired_videos) {
    const gpu::CacheClock::time_point now = gpu::CacheClock::now();
    images_.begin_frame(now);
    const FramePlan plan =
        plan_frame(frame.list, target.width, target.height, &path_hashes_, device_.max_texture_side());
    instances_.write(std::as_bytes(std::span(plan.instances)));
    stops_.write(std::as_bytes(std::span(plan.stops)));

    const wgpu::CommandEncoder encoder = device_.handle().CreateCommandEncoder();
    FrameGroups groups;
    // Gradient stops and color matrices for every pass.
    groups.stops = resources.quads.stops_group(stops_.handle());

    // Path coverage masks first: any pass may fill through them. Masks with the same content as
    // in an earlier frame are reused, so only new ones are triangulated and uploaded, then
    // rendered into the pool. One set of multisampled scratch serves every mask: each is resolved
    // before the next is drawn (WebGPU keeps the order).
    struct NewMask {
        std::size_t path;
        std::uint32_t first_vertex;
        std::uint32_t vertex_count;
    };
    std::vector<NewMask> new_masks;
    path_fans_.clear();
    for (std::size_t i = 0; i < plan.paths.size(); ++i) {
        if (!images_.contains({ImageRole::path_mask, plan.paths[i].key})) {
            const auto first = static_cast<std::uint32_t>(path_fans_.size());
            append_path_fan(plan.paths[i], path_fans_);
            new_masks.push_back({i, first, static_cast<std::uint32_t>(path_fans_.size()) - first});
        }
    }
    if (!new_masks.empty()) {
        path_vertices_.write(std::as_bytes(std::span(path_fans_)));
    }
    for (const NewMask& new_mask : new_masks) {
        const PlanPath& path = plan.paths[new_mask.path];
        if (images_.contains({ImageRole::path_mask, path.key})) {
            continue; // the same path twice in this frame
        }
        const RenderImage& samples = pooled_image(ImageRole::path_samples, 0, gpu::PathRenderer::kMaskFormat,
                                                  path.width, path.height, gpu::PathRenderer::kSamples);
        const RenderImage& stencil =
            pooled_image(ImageRole::path_stencil, 0, gpu::PathRenderer::kStencilFormat, path.width, path.height,
                         gpu::PathRenderer::kSamples, wgpu::TextureUsage::RenderAttachment);
        RenderImage image(device_.handle(), gpu::PathRenderer::kMaskFormat, path.width, path.height);
        resources.paths.render(encoder, samples.view(), stencil.view(), image.view(), path.width, path.height,
                               path_vertices_.handle(), new_mask.first_vertex, new_mask.vertex_count, path.even_odd);
        image.group = image_group(resources, image.view());
        images_.store({ImageRole::path_mask, path.key}, std::move(image),
                      gpu::image_bytes(gpu::PathRenderer::kMaskFormat, path.width, path.height, 1));
    }
    groups.paths.resize(plan.paths.size());
    for (std::size_t i = 0; i < plan.paths.size(); ++i) {
        groups.paths[i] = images_.find({ImageRole::path_mask, plan.paths[i].key})->image.group;
    }

    // Images: uploaded (with their mips) the first time a frame draws them.
    resources.textures.begin_frame(now);
    groups.images.reserve(plan.images.size());
    for (const auto& image : plan.images) {
        groups.images.push_back(
            resources.textures.acquire(encoder, {image->id, image->width, image->height, image->rgba}));
    }
    resources.textures.end_frame();

    // Sample the same snapshots that determined the picture geometry.
    resources.videos.begin_frame(now);
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

        const RenderImage& content =
            pooled_image(ImageRole::layer, i, kLayerFormat, layer.width, layer.height, 1, kLayerUsage);
        draw_pass(encoder, {content.texture(), content.view(), kLayerFormat, layer.width, layer.height, {0, 0, 0, 0}},
                  layer, plan, groups, resources);
        layer_groups.raw = image_group(resources, content.view());
        layer_groups.content = layer_groups.raw;

        if (layer.shadow_sigma) {
            const float sigma = *layer.shadow_sigma;
            const RenderImage& horizontal =
                pooled_image(ImageRole::shadow_horizontal, shadows, gpu::BlurRenderer::kFormat, layer.width,
                             layer.height);
            render_offscreen(encoder, horizontal, "layer shadow", [&](const wgpu::RenderPassEncoder& pass) {
                resources.blur.blur(pass, layer_groups.raw, layer.width, layer.height, true, sigma,
                                    gpu::BlurRenderer::Channel::alpha);
            });
            const wgpu::BindGroup horizontal_group = image_group(resources, horizontal.view());

            const RenderImage& vertical =
                pooled_image(ImageRole::shadow, shadows, gpu::BlurRenderer::kFormat, layer.width, layer.height);
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
            const RenderImage& horizontal =
                pooled_image(ImageRole::inner_horizontal, inner_shadows, gpu::BlurRenderer::kFormat, layer.width,
                             layer.height);
            render_offscreen(encoder, horizontal, "inner shadow", [&](const wgpu::RenderPassEncoder& pass) {
                resources.blur.blur(pass, layer_groups.raw, layer.width, layer.height, true, sigma,
                                    gpu::BlurRenderer::Channel::inverse_alpha);
            });
            const wgpu::BindGroup horizontal_group = image_group(resources, horizontal.view());
            const RenderImage& vertical =
                pooled_image(ImageRole::inner_shadow, inner_shadows, gpu::BlurRenderer::kFormat, layer.width,
                             layer.height);
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
        const RenderImage& image =
            pooled_image(ImageRole::window, 0, kLayerFormat, target.width, target.height, 1, kLayerUsage);
        draw_pass(encoder, {image.texture(), image.view(), kLayerFormat, target.width, target.height, clear_color},
                  plan.main, plan, groups, resources);
        const wgpu::RenderPassEncoder pass = begin_pass(encoder, target.view, clear_color, "window");
        resources.quads.begin(pass, target.format, target.width, target.height, instances_.handle(),
                              image_group(resources, image.view()), groups.stops, empty_group(resources));
        resources.quads.draw(pass, *plan.blit_instance, 1);
        pass.End();
    } else {
        if (target.texture) {
            images_.release({ImageRole::window, 0}); // drawn directly from now on
        }
        draw_pass(encoder, {target.texture, target.view, target.format, target.width, target.height, clear_color},
                  plan.main, plan, groups, resources);
    }
    // Releases what this frame didn't use, as kIntermediateImagePolicy says. (WebGPU keeps an
    // image alive for frames still on the GPU, so releasing one is always safe.)
    images_.end_frame();
    return encoder.Finish();
}

} // namespace elg::detail
