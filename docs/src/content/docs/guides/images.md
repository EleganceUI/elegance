---
title: Images
description: "Showing image files and image data, sizing them, and loading images from the web."
---

`Image` shows an icon, an image file, or encoded image data. Icons are covered in
[Text, fonts, and icons](../text-and-icons/#icons); this page is about raster images.

## Files and data

```cpp
Image({.file = "assets/photo.jpg"})
Image({.file = "assets/logo@2x.png", .scale = 2})  // @2x artwork: half as many points as pixels
Image({.data = embedded_png})                     // std::span<const unsigned char>, copied
```

Supported formats are PNG, JPEG, GIF (the first frame), BMP, TIFF and ICO, plus WebP, HEIF and AVIF
where the system has the codecs. Photos are turned upright using their EXIF orientation.

Images decode in the background and appear once they're ready. Their size is read right away, so
the layout doesn't jump when they appear. A file that can't be read or decoded logs a warning and
shows nothing.

## Sizing

An image is drawn at its own size (its pixels divided by its scale). Make it `.resizable()` to
have it take the size it's offered instead, and keep its proportions with `aspect_ratio`:

```cpp
Image({.file = "photo.jpg"})
    .resizable()
    .aspect_ratio(ContentMode::fill)
    .frame(160, 100)
    .clipped();

Image({.file = "diagram.png"}).resizable().aspect_ratio(ContentMode::fit).frame({.max_width = 400});

Image({.file = "avatar.jpg"}).resizable().frame(48, 48).clip_shape(Circle());
```

`.resizable()` must come first, directly on the image; on other views it throws
`elg::UsageError`. Images are drawn with mipmaps, so scaling them down stays smooth.

## Images from the web

`AsyncImage` downloads an image over HTTP(S) when it's first shown. It shows a spinner while
loading, the image when it arrives, and a placeholder icon if it fails:

```cpp
AsyncImage("https://example.com/avatar.png", {.content_mode = ContentMode::fill})
    .frame(48, 48)
    .clip_shape(Circle());
```

With `content_mode` the image is resizable and keeps its aspect ratio that way; without it, it's
shown at its own size. Downloads are cached in memory, so a list that scrolls back doesn't fetch
the same images again.

To draw each phase yourself, pass a function:

```cpp
AsyncImage(url, [](const AsyncImagePhase& phase) -> View {
    switch (phase.kind) {
    case AsyncImagePhase::Kind::loading: return ProgressView();
    case AsyncImagePhase::Kind::success: return phase.image.resizable().aspect_ratio(ContentMode::fit);
    case AsyncImagePhase::Kind::failure: return Text(phase.error).foreground_style(ThemeColor::danger);
    }
    return View();
});
```

See the [network example](../../examples/#network) for a long list of web images.
