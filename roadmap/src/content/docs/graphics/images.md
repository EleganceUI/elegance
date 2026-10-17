---
title: Raster images
description: Decoding with WIC or stb_image, async loading, a shared GPU texture cache, and the Image view.
sidebar:
  order: 3
---

Images decode off the UI thread, upload on the render thread with mipmaps, and draw through the
same ubershader as everything else. This texture path is what video reuses later.

:::note[Depends on]
[Display list and the UI ubershader](../../renderer/ui-pipeline/) and `post_ui_task` from
[Errors, logging, and crash reports](../../foundations/core/).
:::

## Read first

- `design.md` §13.3 part 1 (Raster images), M5 "Media part 1"
- `include/elg/image.h`, `src/media/image_decode.*`, `src/view/raster_image.*`, `src/view/image.cpp`, `src/gpu/texture_cache.*`

## Tasks

### API
- [ ] `ImageSource` with exactly one of `.file`, `.data`, `.system_name`; `scale` for @2x art. An empty `ImageSource{}` throws
- [ ] `.resizable()` applies to raster images only, must come before other modifiers, and throws otherwise
- [ ] `.aspect_ratio(ContentMode::fit/fill)` and `.aspect_ratio(ratio, mode)` on any view

### Decoding
- [ ] WIC on Windows (PNG, JPEG, GIF, BMP, TIFF, ICO, plus WebP/HEIF/AVIF with OS codecs); stb_image elsewhere
- [ ] Convert to straight-alpha RGBA; apply EXIF orientation; scale anything over 8192 px to fit
- [ ] `probe_image`: read the size from the header without decoding

### Loading
- [ ] Probe the size when shown, so layout is right immediately
- [ ] Decode pixels on a small worker pool and deliver through `post_ui_task` into a `State` the element binds
- [ ] Share assets by source (file path + mtime, or a hash of the data), so an image shown twice decodes once
- [ ] A missing or undecodable image logs a warning and takes no space

### GPU
- [ ] `ImageCommand { rect, uv, image, opacity }`, plus the ubershader's `image` kind (sample linear, premultiply in the shader)
- [ ] `TextureCache` shared by all windows: `R8G8B8A8_SRGB` with a full mip chain made by blits, uploaded inside the frame's own command buffer; staging buffers kept until that slot's fence
- [ ] Drop textures after 600 idle frames, or LRU over a 512 MB budget, never within 16 frames of use
- [ ] Clip stack, `clip_shape`, opacity and layers all apply

## Done when

- [ ] A landscape JPEG fills a rounded frame, a PNG with alpha at @2x is sharp, and a JPEG stored sideways with EXIF orientation 6 shows upright
- [ ] A large photo shown as a 64 pt thumbnail stays smooth (mipmaps)
- [ ] The UI never stalls while a 20 MP image decodes

## Gotchas

- Straight-alpha mips can fringe at transparent edges. Premultiplied mip generation is a follow-up.
- Not built yet: decoding at the drawn size, animated GIF/WebP, SVG files through the path
  renderer, `.interpolation(.none)` for pixel art, and ICC color profiles.
