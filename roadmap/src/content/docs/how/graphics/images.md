---
title: Raster images
description: "Image sources, decoding off the UI thread, sharing assets, and the GPU texture cache with mipmaps."
sidebar:
  order: 3
depends: [framework/shapes-and-effects]
---

## What it's for

Photos, logos and avatars must show quickly, at the right size from the first frame, without the
UI thread ever decoding, and must look smooth when drawn smaller than their pixels. This texture
path is also what video builds on.

## How it works

### Sources

`Image(ImageSource)` is one view for every kind of image. `ImageSource` is an options struct with
**exactly one** of `.file`, `.data` (bytes, e.g. embedded), or `.system_name` (an icon), plus
`scale` for @2x artwork. An empty source throws. Raster images are sized at pixels / scale points
unless `.resizable()`, which makes them take the proposal; combine it with `.aspect_ratio(fit |
fill)` to keep proportions. `.resizable()` must come first and only applies to raster images
(otherwise `UsageError`).

### Loading

1. **Probe now**: when the view is lowered, the file is read (or the data hashed) and the
   header parsed for width, height and EXIF orientation, so layout has the right size in the
   first frame.
2. **Share**: assets are cached by source (path + modification time, or the data's hash), so an
   image shown in ten places decodes once.
3. **Decode in the background**: a small pool of decoder threads decodes to **straight-alpha
   RGBA**, applies the EXIF orientation, and downscales anything over 8192 px.
4. **Deliver**: the result is posted with `post_ui_task` into a `State` holding the pixels. The
   element binds that state, so arrival just repaints it.

A missing or undecodable image logs a warning and takes no space.

Decoders: **WIC** on Windows (PNG, JPEG, GIF, BMP, TIFF, ICO, and WebP/HEIF/AVIF where the OS has
the codecs); **stb_image** elsewhere for now.

### Drawing

The element paints an `ImageCommand { rect, uv, image, opacity }` over its frame. Fit and fill are
layout: `.aspect_ratio(fill)` makes the image's frame larger than the space on one axis, and the
app clips it (`.clipped()`, `.clip_shape(...)`), as in SwiftUI; `fit` leaves room around it. The `DecodedImage` is shared immutably with the render
thread, and carries a process-unique id.

### The texture cache (render thread)

`TextureCache` is shared by all windows:

- **Upload on first draw**, inside that frame's command buffer: copy level 0 from a staging
  buffer, then build a **full mip chain** by blitting each level from the one above, and leave
  every level readable by fragment shaders. The staging buffer is kept until that frame slot's
  fence signals.
- **Format**: `R8G8B8A8_SRGB`, so sampling returns linear color and filtering happens in linear
  space. The shader premultiplies after sampling.
- **Sampling**: linear with mipmaps, so a 4000 px photo drawn as a 64 pt thumbnail stays smooth.
- **Eviction**: textures unused for 600 frames go; over a 512 MB budget, the least recently used
  go first, but never one used in the last 16 frames (it may be in flight).

Clips, `clip_shape`, opacity and layers apply to images like to anything else.

## Rules

- The UI thread never decodes; layout never waits for pixels.
- One decode and one texture per source, however many views show it.
- Free GPU memory only after every frame that may use it has finished.

## Edge cases

- **Straight-alpha mipmaps** bleed color from transparent pixels at edges. Premultiplying before
  generating mips is the fix.
- **Thumbnails decode at full size**; decoding at the drawn size would save memory and time.
- **Animated GIF/WebP** show their first frame.
