// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "render/window_target.h"

#include "core/crash.h"
#include "core/profile.h"
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
#include "render/damage.h"
#include "render/frame_plan.h"
#include "render/intermediate_images.h"
#include "render/render_settings.h"
#include "render/video_textures.h"

#include <webgpu/webgpu_cpp.h>

#include <algorithm>
#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace elg::detail {

namespace {

// Layers hold premultiplied linear color; sRGB storage keeps 8-bit precision where it matters.
// Color blurs read it, and write their own format (gpu::BlurRenderer::kColorFormat).
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
    State(const gpu::Device& device, std::unique_ptr<gpu::Presenter> presenter);

    void render(const WindowFrame& frame, const RenderResources& resources);
    [[nodiscard]] int frames_presented() const noexcept { return frames_presented_; }
    [[nodiscard]] int tiles_drawn() const noexcept { return tiles_drawn_; }

private:
    struct LayerGroups {
        wgpu::BindGroup content; // blurred, when the layer blurs
        wgpu::BindGroup raw;     // as drawn
        wgpu::BindGroup shadow;
        wgpu::BindGroup inner_shadow;
    };

    // The buffers a plan of the frame writes: one set per drawn rect, since every write to a
    // buffer lands before the frame's commands run.
    struct PlanBuffers {
        explicit PlanBuffers(const wgpu::Device& device)
            : instances(device, wgpu::BufferUsage::Vertex), stops(device, wgpu::BufferUsage::Storage),
              path_vertices(device, wgpu::BufferUsage::Vertex) {}
        gpu::Buffer instances;
        gpu::Buffer stops;
        gpu::Buffer path_vertices;
    };

    // A scroll view's tiles: the footprints of the content they were drawn from (its pixels), and
    // the tiles kept, by key, with what of the content each shows.
    struct ScrollTiles {
        std::vector<Footprint> content;
        std::unordered_map<std::uint64_t, Rect> tiles;
    };

    // What the window showed in its last frame drawn: a frame that differs in it draws the whole
    // window again (ELGSPEC-0029.R6).
    struct Shown {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        float scale = 0;
        Color clear;
        std::uint64_t surfaces = 0; // the presenter's surfaces made
        bool operator==(const Shown&) const = default;
    };

    // What a pass draws into. `texture` is set when it can be copied from (layers, the window's
    // kept image): only such passes can read backdrops. `clear`: what it's cleared to first;
    // none: it's drawn over what it holds. `scissor` (whole pixels): the only part drawn, when set.
    struct PassTarget {
        wgpu::Texture texture;
        wgpu::TextureView view;
        wgpu::TextureFormat format{};
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::optional<wgpu::Color> clear;
        std::optional<Rect> scissor;
    };

    // The images and groups a frame's passes sample.
    struct FrameGroups {
        wgpu::Buffer instances;
        wgpu::BindGroup stops;
        std::vector<LayerGroups> layers;
        std::vector<wgpu::BindGroup> paths;
        std::vector<wgpu::BindGroup> images;
        std::vector<wgpu::BindGroup> videos;
    };

    // Records the frame: each of the `drawn` rects planned and drawn over the window's kept image,
    // then the kept image onto the window's `view`, with the `damage` tinted when that's on.
    // `acquired_videos` collects the shared video textures it samples, to release once it's
    // submitted.
    wgpu::CommandBuffer record(const wgpu::TextureView& view, wgpu::TextureFormat format, std::uint32_t width,
                               std::uint32_t height, std::span<const Rect> drawn, std::span<const Rect> damage,
                               const WindowFrame& frame, const RenderResources& resources,
                               std::vector<std::uint64_t>& acquired_videos);
    // Records the plan of one drawn rect into `target`, the kept image.
    void record_plan(const wgpu::CommandEncoder& encoder, const FramePlan& plan, PlanBuffers& buffers,
                     const PassTarget& target, const RenderResources& resources,
                     std::vector<std::uint64_t>& acquired_videos);
    // Records a pass, split wherever its instances read a backdrop.
    void draw_pass(const wgpu::CommandEncoder& encoder, const PassTarget& target, const PlanPass& plan_pass,
                   const FramePlan& plan, const FrameGroups& groups, const RenderResources& resources);
    // Copies a backdrop's region out of `texture` and blurs it as planned; returns its group.
    wgpu::BindGroup take_backdrop(const wgpu::CommandEncoder& encoder, const wgpu::Texture& texture,
                                  const PlanBackdrop& backdrop, const RenderResources& resources);
    // What a blur makes, in pooled images of its own roles: a blurred layer or backdrop (color),
    // a layer's shadow (its alpha), or its inner shadow (its alpha's inverse).
    enum class BlurKind { color, shadow, inner_shadow };
    // Blurs `width` x `height` of `source` as a Gaussian of `sigma` pixels, at 1/`factor` (a power
    // of 2) the resolution: halved that many times first, 2 x 2 texels into one. `slot` picks the
    // pooled images (ImageSlot::index); `clamp_edges`, color only: outside the image repeats its
    // edge. Returns the result's group, which is drawn at the image's size, so its bilinear
    // filtering scales it back up.
    wgpu::BindGroup blur(const wgpu::CommandEncoder& encoder, const wgpu::BindGroup& source, std::uint32_t width,
                         std::uint32_t height, float sigma, std::uint32_t factor, BlurKind kind, bool clamp_edges,
                         std::uint64_t slot, const RenderResources& resources);
    const wgpu::BindGroup& empty_group(const RenderResources& resources);
    // A side of the window's image: the window's (in pixels), up to what the device's textures
    // allow.
    [[nodiscard]] std::uint32_t window_side(int pixels) const;
    // Its scroll views' tiles for this frame: those under content that changed since they were
    // drawn go, and so do all of a scroll view that's gone; a full redraw (render_settings())
    // draws them all again (ELGSPEC-0029.R13).
    void update_tiles(const DisplayList& list);
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
    std::vector<std::unique_ptr<PlanBuffers>> plan_buffers_; // per drawn rect
    gpu::Buffer overlay_; // the kept image onto the window, and the damage tint
    // The window's last frame drawn, which its kept image holds: its commands' footprints, and
    // what it showed.
    std::vector<Footprint> previous_;
    std::optional<Shown> shown_;
    int frames_presented_ = 0;
    // Its scroll views' tiles, by the scroll view's id (ELGSPEC-0029.R13).
    std::unordered_map<std::uint64_t, ScrollTiles> scrolls_;
    int tiles_drawn_ = 0;
    std::optional<RenderImage> empty_;    // bound where no second image is
    wgpu::BindGroup empty_group_;
};

WindowTarget::WindowTarget(const gpu::Device& device, const gpu::SurfaceSource& surface, bool transparent,
                           void* native_window)
    : WindowTarget(device, gpu::create_presenter(device, surface, transparent, native_window)) {}

WindowTarget::WindowTarget(const gpu::Device& device, std::unique_ptr<gpu::Presenter> presenter)
    : state_(std::make_unique<State>(device, std::move(presenter))) {}

WindowTarget::~WindowTarget() = default;

int WindowTarget::frames_presented() const noexcept {
    return state_->frames_presented();
}

int WindowTarget::tiles_drawn() const noexcept {
    return state_->tiles_drawn();
}

void WindowTarget::render(const WindowFrame& frame, const RenderResources& resources) {
    state_->render(frame, resources);
}

WindowTarget::State::State(const gpu::Device& device, std::unique_ptr<gpu::Presenter> presenter)
    : device_(device), presenter_(std::move(presenter)),
      sampler_(gpu::create_linear_sampler(device.handle())), overlay_(device.handle(), wgpu::BufferUsage::Vertex) {
}

void WindowTarget::State::render(const WindowFrame& frame, const RenderResources& resources) {
    ELG_ZONE("Render window");
    const std::uint32_t width = window_side(frame.pixel_width);
    const std::uint32_t height = window_side(frame.pixel_height);
    const Color clear = frame.list.clear_color;
    // What changed since the last frame drawn, which the kept image holds; all of it when that's
    // no longer what the window shows (ELGSPEC-0029.R6).
    const Shown shown{width, height, frame.list.scale, clear, presenter_->surfaces_made()};
    const Rect window{0, 0, static_cast<float>(width), static_cast<float>(height)};
    bool whole = shown_ != shown || !images_.contains({ImageRole::kept, 0}) || render_settings().full_redraw;
    std::vector<Footprint> now;
    std::vector<Rect> damage;
    {
        ELG_ZONE("Damage");
        now = footprints(frame.list, width, height, &path_hashes_);
        damage = whole ? std::vector<Rect>{window} : damage_between(previous_, now);
    }
    if (damage.empty()) {
        // Nothing changed: nothing is drawn or presented (ELGSPEC-0029.R7). The device is still
        // looked at, so that its loss shows without a frame to draw.
        path_hashes_.end_frame();
        device_.tick();
        return;
    }
    const wgpu::TextureView view = presenter_->begin_frame(width, height, {clear.r, clear.g, clear.b, 1});
    if (!view) {
        path_hashes_.end_frame();
        return; // minimized, or skipped: the kept image still holds the last frame drawn
    }
    if (presenter_->surfaces_made() != shown.surfaces && !whole) {
        whole = true; // a new surface, made just now
        damage = {window};
    }
    const std::vector<Rect> drawn = whole ? std::vector<Rect>{window} : drawn_area(damage);
    update_tiles(frame.list);

    std::vector<std::uint64_t> acquired_videos;
    const wgpu::CommandBuffer commands = record(view, presenter_->view_format(), presenter_->width(),
                                                presenter_->height(), drawn, damage, frame, resources, acquired_videos);
    device_.submit(commands);
    resources.videos.release(acquired_videos); // back to the decoder
    presenter_->end_frame(false, drawn);
    device_.tick();
    previous_ = std::move(now);
    shown_ = Shown{width, height, frame.list.scale, clear, presenter_->surfaces_made()};
    ++frames_presented_;
}

void WindowTarget::State::update_tiles(const DisplayList& list) {
    ELG_ZONE("Damage"); // the scroll views' content, and the tiles it no longer matches
    const auto release = [&](ScrollTiles& scroll, const auto& which) {
        std::erase_if(scroll.tiles, [&](const auto& tile) {
            if (!which(tile.second)) {
                return false;
            }
            images_.release({ImageRole::tile, tile.first});
            return true;
        });
    };
    const bool full = render_settings().full_redraw;
    std::unordered_map<std::uint64_t, ScrollTiles> seen;
    for (ScrollContent& content : scroll_contents(list, &path_hashes_)) {
        const auto found = scrolls_.find(content.id);
        ScrollTiles scroll = found != scrolls_.end() ? std::move(found->second) : ScrollTiles{};
        const std::vector<Rect> changed = full ? std::vector<Rect>{} : damage_between(scroll.content, content.footprints);
        release(scroll, [&](const Rect& shows) {
            return full || std::ranges::any_of(changed, [&](const Rect& r) {
                       return r.x < shows.max_x() && shows.x < r.max_x() && r.y < shows.max_y() && shows.y < r.max_y();
                   });
        });
        scroll.content = std::move(content.footprints);
        seen.insert_or_assign(content.id, std::move(scroll));
    }
    for (auto& [id, scroll] : scrolls_) {
        if (!seen.contains(id)) {
            release(scroll, [](const Rect&) { return true; });
        }
    }
    scrolls_ = std::move(seen);
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

wgpu::BindGroup WindowTarget::State::blur(const wgpu::CommandEncoder& encoder, const wgpu::BindGroup& source,
                                          std::uint32_t width, std::uint32_t height, float sigma, std::uint32_t factor,
                                          BlurKind kind, bool clamp_edges, std::uint64_t slot,
                                          const RenderResources& resources) {
    using Channel = gpu::BlurRenderer::Channel;
    struct Roles {
        ImageRole down;
        ImageRole horizontal;
        ImageRole result;
        wgpu::TextureFormat format;
        Channel first; // what the first pass takes of the source's texels
        const char* label;
    };
    const Roles roles =
        kind == BlurKind::color    ? Roles{ImageRole::color_down, ImageRole::color_horizontal, ImageRole::color_blurred,
                                        gpu::BlurRenderer::kColorFormat, Channel::color, "blur"}
        : kind == BlurKind::shadow ? Roles{ImageRole::shadow_down, ImageRole::shadow_horizontal, ImageRole::shadow,
                                           gpu::BlurRenderer::kFormat, Channel::alpha, "layer shadow"}
                                   : Roles{ImageRole::inner_down, ImageRole::inner_horizontal, ImageRole::inner_shadow,
                                           gpu::BlurRenderer::kFormat, Channel::inverse_alpha, "inner shadow"};
    // Outside the image: transparent, which an inner shadow reads as full; from the second pass
    // on, an inner shadow's single channel repeats its edge instead, as a color blur may.
    Channel channel = roles.first;
    const auto clamps = [&] { return kind == BlurKind::color ? clamp_edges : kind == BlurKind::inner_shadow && channel == Channel::red; };
    const auto next = [&] {
        if (kind != BlurKind::color) {
            channel = Channel::red; // one channel from the first pass on
        }
    };

    wgpu::BindGroup input = source;
    std::uint64_t level = 0;
    for (std::uint32_t done = 1; done < factor; done *= 2, ++level) {
        const std::uint32_t half_width = (width + 1) / 2;
        const std::uint32_t half_height = (height + 1) / 2;
        const RenderImage& down = pooled_image(roles.down, slot ^ (level << 56), roles.format, half_width, half_height);
        render_offscreen(encoder, down, "downsample", [&](const wgpu::RenderPassEncoder& pass) {
            resources.blur.downsample(pass, input, width, height, 2, clamps(), channel);
        });
        input = image_group(resources, down.view());
        width = half_width;
        height = half_height;
        next();
    }
    // The blur is that of the source's pixels as squares, of sigma^2 + 1/12 variance in pixels.
    // At the lower resolution, the downsampling's boxes, the kernel's squares and the bilinear
    // filtering that scales the result back up add about factor^2 / 3 of it; the kernel's sigma
    // adds the rest.
    float texel_sigma = sigma;
    if (factor > 1) {
        const auto f = static_cast<float>(factor);
        texel_sigma = std::sqrt(std::max(sigma * sigma + 1.f / 6 - f * f / 3, 0.25f * f * f)) / f;
    }
    const RenderImage& horizontal = pooled_image(roles.horizontal, slot, roles.format, width, height);
    render_offscreen(encoder, horizontal, roles.label, [&](const wgpu::RenderPassEncoder& pass) {
        resources.blur.blur(pass, input, width, height, true, texel_sigma, channel, clamps());
    });
    next();
    const wgpu::BindGroup horizontal_group = image_group(resources, horizontal.view());
    const RenderImage& vertical = pooled_image(roles.result, slot, roles.format, width, height);
    render_offscreen(encoder, vertical, roles.label, [&](const wgpu::RenderPassEncoder& pass) {
        resources.blur.blur(pass, horizontal_group, width, height, false, texel_sigma, channel, clamps());
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
    return blur(encoder, image_group(resources, copy.view()), backdrop.width, backdrop.height, backdrop.sigma,
                backdrop.downsample, BlurKind::color, true, kBackdropBlurSlot, resources);
}

void WindowTarget::State::draw_pass(const wgpu::CommandEncoder& encoder, const PassTarget& target,
                                    const PlanPass& plan_pass, const FramePlan& plan, const FrameGroups& groups,
                                    const RenderResources& resources) {
    gpu::QuadRenderer& quads = resources.quads;
    wgpu::BindGroup second = empty_group(resources);
    wgpu::RenderPassEncoder pass;
    const auto begin = [&](bool first) {
        pass = begin_pass(encoder, target.view, first ? target.clear : std::nullopt, "pass");
        quads.begin(pass, target.format, target.width, target.height, groups.instances, resources.atlas, groups.stops,
                    second);
        if (target.scissor) {
            // Rounded out, and in the target, as WebGPU requires.
            const auto edge = [](float v, std::uint32_t side) {
                return static_cast<std::uint32_t>(std::clamp(v, 0.f, static_cast<float>(side)));
            };
            const Rect& s = *target.scissor;
            const std::uint32_t x0 = edge(std::floor(s.x), target.width);
            const std::uint32_t y0 = edge(std::floor(s.y), target.height);
            const std::uint32_t x1 = edge(std::ceil(s.max_x()), target.width);
            const std::uint32_t y1 = edge(std::ceil(s.max_y()), target.height);
            pass.SetScissorRect(x0, y0, std::max(x0, x1) - x0, std::max(y0, y1) - y0);
        }
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
        } else if (segment.tile >= 0) {
            // Drawn by this plan, or kept: needed by this frame either way.
            const auto* tile = images_.find({ImageRole::tile, plan.tiles[static_cast<std::size_t>(segment.tile)]});
            image = tile ? tile->image.group : wgpu::BindGroup();
            if (!image) {
                continue;
            }
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

wgpu::CommandBuffer WindowTarget::State::record(const wgpu::TextureView& view, wgpu::TextureFormat format,
                                                std::uint32_t width, std::uint32_t height, std::span<const Rect> drawn,
                                                std::span<const Rect> damage, const WindowFrame& frame,
                                                const RenderResources& resources,
                                                std::vector<std::uint64_t>& acquired_videos) {
    ELG_ZONE("Record");
    const gpu::CacheClock::time_point now = gpu::CacheClock::now();
    images_.begin_frame(now);
    resources.textures.begin_frame(now);
    resources.videos.begin_frame(now);
    const wgpu::CommandEncoder encoder = device_.handle().CreateCommandEncoder();

    // The window's kept image, which holds its last frame drawn: each drawn rect is drawn over it,
    // as a plan of its own (ELGSPEC-0029.R3). It's needed by every frame (ELGSPEC-0029.R9).
    const RenderImage& kept = pooled_image(ImageRole::kept, 0, kLayerFormat, width, height, 1, kLayerUsage);
    PassTarget target{kept.texture(), kept.view(), kLayerFormat, width, height, std::nullopt, std::nullopt};
    while (plan_buffers_.size() < drawn.size()) {
        plan_buffers_.push_back(std::make_unique<PlanBuffers>(device_.handle()));
    }
    const std::function<bool(std::uint64_t)> kept_tiles = [this](std::uint64_t key) {
        return images_.contains({ImageRole::tile, key});
    };
    for (std::size_t i = 0; i < drawn.size(); ++i) {
        const FramePlan plan = plan_frame(frame.list, width, height, &path_hashes_, device_.max_texture_side(),
                                          drawn[i], kept_tiles);
        // Only the rect's pixels: what draws there reaches no further (its anti-aliasing, the
        // clearing mask, which clears all it covers).
        target.scissor = drawn[i];
        record_plan(encoder, plan, *plan_buffers_[i], target, resources, acquired_videos);
    }
    path_hashes_.end_frame();
    resources.textures.end_frame();
    resources.videos.end_frame();

    // The kept image onto the window, and the damage tinted (ELGSPEC-0029.R11).
    const std::vector<gpu::QuadInstance> overlay =
        window_overlay(width, height, render_settings().show_damage ? damage : std::span<const Rect>());
    overlay_.write(std::as_bytes(std::span(overlay)));
    const wgpu::RenderPassEncoder pass = begin_pass(encoder, view, wgpu::Color{0, 0, 0, 0}, "window");
    resources.quads.begin(pass, format, width, height, overlay_.handle(), image_group(resources, kept.view()),
                          resources.quads.stops_group(plan_buffers_[0]->stops.handle()), empty_group(resources));
    resources.quads.draw(pass, 0, static_cast<std::uint32_t>(overlay.size()));
    pass.End();
    // Releases what this frame didn't use, as kIntermediateImagePolicy says. (WebGPU keeps an
    // image alive for frames still on the GPU, so releasing one is always safe.)
    images_.end_frame();
    return encoder.Finish();
}

void WindowTarget::State::record_plan(const wgpu::CommandEncoder& encoder, const FramePlan& plan,
                                      PlanBuffers& buffers, const PassTarget& target,
                                      const RenderResources& resources,
                                      std::vector<std::uint64_t>& acquired_videos) {
    buffers.instances.write(std::as_bytes(std::span(plan.instances)));
    buffers.stops.write(std::as_bytes(std::span(plan.stops)));

    FrameGroups groups;
    groups.instances = buffers.instances.handle();
    // Gradient stops and color matrices for every pass.
    groups.stops = resources.quads.stops_group(buffers.stops.handle());

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
        buffers.path_vertices.write(std::as_bytes(std::span(path_fans_)));
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
                               buffers.path_vertices.handle(), new_mask.first_vertex, new_mask.vertex_count, path.even_odd);
        image.group = image_group(resources, image.view());
        images_.store({ImageRole::path_mask, path.key}, std::move(image),
                      gpu::image_bytes(gpu::PathRenderer::kMaskFormat, path.width, path.height, 1));
    }
    groups.paths.resize(plan.paths.size());
    for (std::size_t i = 0; i < plan.paths.size(); ++i) {
        groups.paths[i] = images_.find({ImageRole::path_mask, plan.paths[i].key})->image.group;
    }

    // Images: uploaded (with their mips) the first time a frame draws them.
    groups.images.reserve(plan.images.size());
    for (const auto& image : plan.images) {
        groups.images.push_back(
            resources.textures.acquire(encoder, {image->id, image->width, image->height, image->rgba}));
    }

    // Sample the same snapshots that determined the picture geometry.
    for (const auto& video_frame : plan.video_frames) {
        groups.videos.push_back(video_frame ? resources.videos.acquire(encoder, *video_frame, acquired_videos)
                                            : wgpu::BindGroup());
    }

    // Offscreen layers first, innermost first; each may also be blurred, and get a two-pass
    // blurred shadow and inner shadow.
    groups.layers.resize(plan.layers.size());
    std::size_t shadows = 0;
    std::size_t inner_shadows = 0;
    for (std::size_t i = 0; i < plan.layers.size(); ++i) {
        const PlanPass& layer = plan.layers[i];
        LayerGroups& layer_groups = groups.layers[i];
        if (layer.tile) {
            // A scroll view's tile: kept by its key, for the frames after this one too.
            RenderImage tile(device_.handle(), kLayerFormat, layer.width, layer.height, 1, kLayerUsage);
            draw_pass(encoder,
                      {tile.texture(), tile.view(), kLayerFormat, layer.width, layer.height, wgpu::Color{0, 0, 0, 0},
                       std::nullopt},
                      layer, plan, groups, resources);
            tile.group = image_group(resources, tile.view());
            images_.store({ImageRole::tile, layer.tile->key}, std::move(tile),
                          gpu::image_bytes(kLayerFormat, layer.width, layer.height, 1));
            scrolls_[layer.tile->scroll].tiles.insert_or_assign(layer.tile->key, layer.tile->content);
            ++tiles_drawn_;
            continue;
        }

        const RenderImage& content =
            pooled_image(ImageRole::layer, i, kLayerFormat, layer.width, layer.height, 1, kLayerUsage);
        draw_pass(encoder,
                  {content.texture(), content.view(), kLayerFormat, layer.width, layer.height, wgpu::Color{0, 0, 0, 0},
                   std::nullopt},
                  layer, plan, groups, resources);
        layer_groups.raw = image_group(resources, content.view());
        layer_groups.content = layer_groups.raw;

        if (layer.shadow_sigma) {
            layer_groups.shadow = blur(encoder, layer_groups.raw, layer.width, layer.height, *layer.shadow_sigma,
                                       layer.shadow_downsample, BlurKind::shadow, false, shadows, resources);
            ++shadows;
        }
        if (layer.inner_shadow_sigma) {
            // The blurred inverse of the alpha: full outside the content, fading in from its edges.
            layer_groups.inner_shadow =
                blur(encoder, layer_groups.raw, layer.width, layer.height, *layer.inner_shadow_sigma,
                     layer.inner_shadow_downsample, BlurKind::inner_shadow, false, inner_shadows, resources);
            ++inner_shadows;
        }
        if (layer.blur_sigma) {
            layer_groups.content = blur(encoder, layer_groups.raw, layer.width, layer.height, *layer.blur_sigma,
                                        layer.blur_downsample, BlurKind::color, layer.blur_clamp, i, resources);
        }
    }

    // The drawn rect itself, over what the kept image holds.
    draw_pass(encoder, target, plan.main, plan, groups, resources);
}

} // namespace elg::detail
