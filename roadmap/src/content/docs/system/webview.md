---
title: WebView
description: Embedding the system's browser control (WebView2, WKWebView, WebKitGTK) as a view.
sidebar:
  order: 3
---

Some apps need a web page inside them: an OAuth sign-in, a help page, a rich-text or map widget.
Bundling a browser engine is a non-goal (`design.md` §1), so a `WebView` hosts the **OS's own**
browser control instead. It costs nothing to ship and stays patched by the OS. The hard part is
that this control draws itself, outside Elegance's renderer.

:::caution[Decide first]
This relaxes the "no web content" non-goal. Record the decision in `design.md` §1 before building
it (see [Open decisions](../../beyond/decisions/)). The exception being made is "the system
browser control, as a view," not "Elegance renders HTML."
:::

:::note[Depends on]
[Windows, title bars, and DirectComposition](../../controls/windows/) (the web content joins that
visual tree), [Hit testing, events, and focus](../../framework/input-and-focus/),
[Scrolling and lists](../../controls/scrolling-and-lists/) (clipping and culling), and
`post_ui_task`.
:::

## Read first

- [WebView2 docs](https://learn.microsoft.com/microsoft-edge/webview2/): "Windowed vs. Visual hosting", `ICoreWebView2CompositionController`, the Evergreen runtime and its bootstrapper
- `src/gpu/composition_win32.*`: every Elegance window already presents through a DirectComposition visual. The web view's visual goes beside it
- `include/elg/video.h`: the closest existing pattern (a handle with reactive state, plus a view that shows it)

## Tasks

### API
- [ ] `WebView` handle (copies share one browser, closed with the last copy): `WebView({.url, .user_agent, .user_data_folder, .dev_tools})`
- [ ] Navigation: `load(url)`, `load_html(string)`, `reload()`, `stop()`, `go_back()`, `go_forward()`
- [ ] Reactive state: `url()`, `title()`, `is_loading()`, `progress()`, `can_go_back()`, `can_go_forward()`, `error()`
- [ ] Script bridge: `evaluate(js, done(result_json))`, `post_message(json)`, `{.on_message = [](const std::string& json) {...}}` (`window.chrome.webview.postMessage` on the page side)
- [ ] Policy: `{.on_navigate = [](const std::string& url) -> bool}` to allow or block navigations, plus new-window requests (open in the system browser by default)
- [ ] `WebViewView(web)`: fills the space offered; ideal size about 400×300
- [ ] Every callback runs on the UI thread, so it can write `State` directly

### Windows backend (WebView2)
- [ ] Get the WebView2 SDK (vcpkg `webview2` port, or the NuGet package) and **load `WebView2Loader.dll` dynamically**, so apps that never make a WebView don't pay for it
- [ ] The UI thread must be COM STA (`CoInitializeEx(COINIT_APARTMENTTHREADED)`) and pump messages. SDL's loop already pumps
- [ ] Runtime missing: `error()` says so, with a link to the Evergreen bootstrapper. Never crash
- [ ] Create the environment once per process (one user data folder; default under `pref_path()`)
- [ ] **Visual hosting** (`CreateCoreWebView2CompositionController`): set its `RootVisualTarget` to a DirectComposition visual added **above** the window's Elegance content visual, on the same DComp device
- [ ] Each layout, send the element's window frame (points × scale) to the visual's offset and the controller's `Bounds`
- [ ] Clip to the enclosing clips with an `IDCompositionRectangleClip`. It supports corner radii, so `clip_shape(RoundedRectangle)` works
- [ ] Hide the visual when culled (scrolled out of view), when an ancestor has zero opacity, or while a transition removes the view
- [ ] Input: with visual hosting the host forwards mouse and wheel input (`SendMouseInput`) and pointer input (`SendPointerInput`), translated to the view's coordinates; set the cursor from `CursorChanged`
- [ ] Focus: joining Elegance's `FocusManager` calls `MoveFocus(PROGRAMMATIC/NEXT/PREVIOUS)`; `MoveFocusRequested` (Tab out of the page) moves Elegance focus to the next or previous element
- [ ] Keys: while the page has focus, keyboard input goes to WebView2. Handle `AcceleratorKeyPressed` so app shortcuts (F12 inspector, Ctrl+Q) still reach Elegance
- [ ] Scale changes: update `RasterizationScale` when the window's DPI changes
- [ ] Shutdown: close controllers before the window and the DComp device go

### Keeping it in sync with Elegance frames
- [ ] Work out which thread commits DirectComposition: the visual's position must change in the **same commit** as the Vulkan frame that moved the content around it, or the page lags a frame behind while scrolling and resizing
- [ ] Measure during a scroll and a live resize; if they drift, hand position updates to the render thread with the frame packet and commit them there with the present

### Other platforms
- [ ] macOS: `WKWebView` as an `NSView` over the Metal/MoltenVK layer; `WKScriptMessageHandler` for messages
- [ ] Linux: WebKitGTK. It needs a GTK main loop next to SDL's; investigate before promising it
- [ ] Until a backend exists, the view shows a placeholder and `error()` explains

### Accessibility and tools
- [ ] WebView2 exposes its own UI Automation tree. Find out whether AccessKit can parent a foreign UIA subtree; if not, expose the view as one labeled element and document the gap
- [ ] Inspector: show the URL and loading state; `.dev_tools` opens the browser's DevTools

## Done when

- [ ] An OAuth sign-in page loads in a rounded, clipped view inside a List and scrolls with the list with no visible lag
- [ ] Tab moves from an Elegance text field into the page's first input and back out again
- [ ] A page calls `postMessage`, and the app updates a `State` from it; `evaluate` returns a value
- [ ] On a machine without the WebView2 runtime, the app keeps running and shows the error
- [ ] Popups and menus still appear above the web content (they're separate windows, so they should)

## Gotchas to expect

- **Airspace**: native web content always draws above Elegance content in the same window. Nothing
  Elegance draws can overlap it, including overlays, shadows over it, a tooltip drawn in-window,
  `.mask()`, opacity groups, and blur behind. WebView2 has no offscreen-texture API (only
  `CapturePreview` screenshots), so that's a hard limit. Document it, and reach for popup windows
  for anything that must sit on top.
- Transforms (`scale` transitions, rotations) can't apply to the web content. Fade or hide it
  during them instead.
- WebView2 creation is asynchronous and takes a noticeable fraction of a second the first time.
  Show a loading placeholder, and queue requests made before it's ready (as `VideoPlayer` does).
- The user data folder is locked per process. Two apps (or two copies) sharing one folder fail to
  start their environments.
- A bundled engine (CEF with off-screen rendering into a texture) would avoid the airspace problem
  but brings back exactly the size and cost the design rejects. Keep it as a possible separate,
  optional package, never part of the core.
