// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Appearance on macOS (platform/generic/native.h): window materials as vibrancy
// (NSVisualEffectView behind the content), and the accent color the person chose in System
// Settings > Appearance. Compiled with ARC.

#include "platform/generic/native.h"
#include "platform/macos/appearance.h"

#include <SDL3/SDL.h>

#import <AppKit/AppKit.h>

namespace elg::detail::platform {

namespace macos {

void apply_material(void* ns_window, WindowMaterial material) {
    NSWindow* window = (__bridge NSWindow*)ns_window;
    NSView* content = window.contentView;
    if (!content || material == WindowMaterial::none) {
        return;
    }
    NSVisualEffectView* effect = [[NSVisualEffectView alloc] initWithFrame:content.bounds];
    switch (material) {
    case WindowMaterial::sidebar: effect.material = NSVisualEffectMaterialSidebar; break;
    case WindowMaterial::acrylic: effect.material = NSVisualEffectMaterialHUDWindow; break;
    default: effect.material = NSVisualEffectMaterialUnderWindowBackground; break;
    }
    effect.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    effect.state = NSVisualEffectStateFollowsWindowActiveState;
    effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    // Below SDL's Metal view, which draws the content over it.
    [content addSubview:effect positioned:NSWindowBelow relativeTo:nil];
}

} // namespace macos

namespace native {

namespace {

id g_colors_observer = nil;

} // namespace

bool supports_material(WindowMaterial material) {
    return material != WindowMaterial::none;
}

void appearance_changed(SDL_Window*, WindowParts&) {
    // Vibrancy follows the system's appearance on its own.
}

std::optional<Color> system_accent_color() {
    NSColor* accent = [NSColor.controlAccentColor colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    if (!accent) {
        return std::nullopt;
    }
    return Color{static_cast<float>(accent.redComponent), static_cast<float>(accent.greenComponent),
                 static_cast<float>(accent.blueComponent), static_cast<float>(accent.alphaComponent)};
}

void watch_appearance() {
    // SDL reports light/dark changes, not the accent's.
    g_colors_observer = [NSNotificationCenter.defaultCenter addObserverForName:NSSystemColorsDidChangeNotification
                                                                        object:nil
                                                                         queue:NSOperationQueue.mainQueue
                                                                    usingBlock:^(NSNotification*) {
                                                                      SDL_Event event{};
                                                                      event.type = SDL_EVENT_SYSTEM_THEME_CHANGED;
                                                                      SDL_PushEvent(&event);
                                                                    }];
}

void unwatch_appearance() {
    if (g_colors_observer) {
        [NSNotificationCenter.defaultCenter removeObserver:g_colors_observer];
        g_colors_observer = nil;
    }
}

} // namespace native

} // namespace elg::detail::platform
