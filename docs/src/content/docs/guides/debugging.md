---
title: Debugging and errors
description: "The inspector, logging, how Elegance reports errors, and crash reports."
---

## The inspector

Press **F12** in any window to open the inspector for it, in a window of its own. Press F12 again,
or close either window, to close it.

- **The tree** lists every element with a summary (its text, a shape's radius, a stack's child
  count, effects such as clips, opacity and shadows) and its size. It follows the window as it
  changes.
- **Selecting** a row highlights the element in the window, and shows its frame, the size it was
  last proposed, and its properties: fonts, colors, fills and strokes.
- **Pick** mode outlines the element under the pointer; click to select it in the tree, or press
  Esc to cancel.
- **Frame stats** show the UI thread's build time, the view count, and the numbers of drawing
  commands, glyphs and paths. Offscreen passes, which cost more, are flagged.

Because it's a separate window, the inspector never changes the layout it's showing.

## Logging

Elegance logs to stderr, with a prefix per level:

```
[elg:info] GPU: AMD Radeon RX 6650 XT (Direct3D 12 through WebGPU)
[elg:warning] image: can't read assets/missing.png
```

Debug builds also log debug messages: fonts loaded, the presentation path chosen, and more. Run
your app from a terminal, or look at the IDE's output, when something doesn't appear.

## Errors you can handle

Elegance reports problems that are yours to handle by throwing exceptions derived from
`elg::Error` (itself a `std::runtime_error`):

| Exception | When |
| --- | --- |
| `elg::UsageError` | The API was used incorrectly: an unknown icon name, an invalid SVG path, `.resizable()` on something that isn't an image, state used off the UI thread. The message says what was wrong. |
| `elg::ResourceError` | A font, sound or file failed to load. |
| `elg::GpuError` | No suitable GPU, out of GPU memory, or a lost device that couldn't be recovered. |
| `elg::PlatformError` | The operating system or windowing layer failed. |

Exceptions thrown by your own callbacks (event handlers, effects, content functions) propagate out
of `app.run()` after a clean shutdown, so a top-level `try` sees them:

```cpp
int main() {
    try {
        elg::App app;
        app.window({.title = "App"}, Root);
        return app.run();
    } catch (const elg::Error& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
```

In debug builds, each `elg::Error` also captures a stack trace where it was thrown.

## Crash reports

Bugs inside Elegance itself aren't thrown: a failed internal check produces a **crash report** and
ends the process, since continuing could corrupt state. The report has the message, where in
Elegance it happened, a stack trace, the Elegance version and the most recent log lines. On Windows
it's written to `%APPDATA%\Elegance\Elegance\crashes\`, and a message box shows where.

If you hit one, please report it with the file attached.
