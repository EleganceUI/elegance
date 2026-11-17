// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_GPU_SURFACE_SOURCE_H
#define ELG_GPU_SURFACE_SOURCE_H

// What WebGPU makes a window's surface from: the window system's native handles. The app takes
// them from the platform's window (platform::SurfaceSource) and hands them to the render thread.

#include <cstdint>

namespace elg::detail::gpu {

struct SurfaceSource {
    enum class Kind { none, win32, metal, xlib, wayland };
    Kind kind = Kind::none;
    void* display = nullptr; // HINSTANCE | Display* | wl_display*
    void* handle = nullptr;  // HWND | CAMetalLayer* | wl_surface*
    std::uint64_t xlib_window = 0;
};

} // namespace elg::detail::gpu

#endif // ELG_GPU_SURFACE_SOURCE_H
