// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "platform/macos/title_bar.h"

#import <AppKit/AppKit.h>

// Keeps a window's title bar integrated and its traffic lights where they were asked to be: AppKit
// lays the title bar out again when the window resizes or changes key state, putting them back.
@interface ElgTitleBar : NSObject
- (instancetype)initWithWindow:(NSWindow*)window position:(std::optional<elg::Point>)position;
- (void)integrate;
// In points from the content's top-left corner.
- (NSRect)controlsFrame;
- (void)stop;
@end

@implementation ElgTitleBar {
    __weak NSWindow* _window;
    std::optional<elg::Point> _position;
    NSMutableArray<id>* _observers;
    CGFloat _spacing; // between the buttons' left edges, as AppKit lays them out
    BOOL _placing;    // moving the buttons changes their frames too
    BOOL _pending;    // a placement waits for AppKit's layout pass to finish
    NSRect _windowed;     // the last frame before fullscreen, put back after it
    BOOL _fullscreen;     // from entering fullscreen until the title bar is integrated again
}

- (instancetype)initWithWindow:(NSWindow*)window position:(std::optional<elg::Point>)position {
    if ((self = [super init])) {
        _window = window;
        _position = position;
        _observers = [NSMutableArray array];
        [self observe:NSWindowDidResizeNotification of:window];
        __weak ElgTitleBar* weak = self;
        [_observers addObject:[NSNotificationCenter.defaultCenter addObserverForName:NSWindowWillEnterFullScreenNotification
                                                                               object:window
                                                                                queue:nil
                                                                           usingBlock:^(NSNotification*) {
                                                                               ElgTitleBar* strong = weak;
                                                                               if (strong) {
                                                                                   strong->_fullscreen = YES;
                                                                               }
                                                                           }]];
        // AppKit lays the title bar out again on its own (showing the window, key changes): it
        // moves the buttons, or resizes their container.
        NSArray<NSButton*>* buttons = [self buttons];
        _spacing = buttons.count > 1 ? NSMinX(buttons[1].frame) - NSMinX(buttons[0].frame) : 0;
        for (NSButton* button in buttons) {
            [self observe:NSViewFrameDidChangeNotification of:button];
        }
        if (NSView* container = buttons.firstObject.superview.superview) {
            [self observe:NSViewFrameDidChangeNotification of:container];
        }
    }
    return self;
}

- (void)observe:(NSNotificationName)name of:(id)object {
    __weak ElgTitleBar* weak = self;
    [_observers addObject:[NSNotificationCenter.defaultCenter addObserverForName:name
                                                                           object:object
                                                                            queue:nil
                                                                       usingBlock:^(NSNotification*) {
                                                                           [weak placeButtonsAfterLayout];
                                                                       }]];
}

// The notifications arrive in the middle of AppKit's layout, which may move the other buttons
// afterwards: place them now (no frame with them elsewhere), and again once it's done.
- (void)placeButtonsAfterLayout {
    [self placeButtons];
    if (_pending) {
        return;
    }
    _pending = YES;
    __weak ElgTitleBar* weak = self;
    dispatch_async(dispatch_get_main_queue(), ^{
        ElgTitleBar* strong = weak;
        if (strong) {
            strong->_pending = NO;
            [strong placeButtons];
        }
    });
}

- (NSArray<NSButton*>*)buttons {
    NSMutableArray<NSButton*>* buttons = [NSMutableArray array];
    for (NSNumber* kind in @[@(NSWindowCloseButton), @(NSWindowMiniaturizeButton), @(NSWindowZoomButton)]) {
        if (NSButton* button = [_window standardWindowButton:static_cast<NSWindowButton>(kind.integerValue)]) {
            [buttons addObject:button];
        }
    }
    return buttons;
}

- (void)integrate {
    NSWindow* window = _window;
    if (!window || (window.styleMask & NSWindowStyleMaskFullScreen)) {
        return;
    }
    if (!(window.styleMask & NSWindowStyleMaskFullSizeContentView)) {
        // setStyleMask: unhooks the content view from SDL's listener (SDL_cocoawindow.m does the same).
        NSView* content = window.contentView;
        NSResponder* next = content.nextResponder;
        window.styleMask |= NSWindowStyleMaskFullSizeContentView;
        if (content.nextResponder != next) {
            content.nextResponder = next;
        }
        // The content view grew under the title bar without the window resizing, so SDL wouldn't
        // notice: tell it (the delegate observes this).
        [NSNotificationCenter.defaultCenter postNotificationName:NSWindowDidResizeNotification object:window];
    }
    if (_fullscreen) {
        // Leaving fullscreen, AppKit and SDL each restore a frame for a plain title bar.
        _fullscreen = NO;
        if (!NSIsEmptyRect(_windowed) && !NSEqualRects(_windowed, window.frame)) {
            [window setFrame:_windowed display:YES];
        }
    }
    window.titlebarAppearsTransparent = YES;
    window.titleVisibility = NSWindowTitleHidden;
    [self placeButtons];
}

- (void)placeButtons {
    NSWindow* window = _window;
    if (!window || _fullscreen || (window.styleMask & NSWindowStyleMaskFullScreen)) {
        return;
    }
    // SDL makes the title bar plain as fullscreen begins, before _fullscreen is set.
    if (window.styleMask & NSWindowStyleMaskFullSizeContentView) {
        _windowed = window.frame;
    }
    if (_placing || !_position) {
        return;
    }
    NSArray<NSButton*>* buttons = [self buttons];
    if (buttons.count == 0) {
        return;
    }
    // The buttons sit in the title bar view, inside its container: grow the container so they fit
    // where they go, then move them, keeping AppKit's spacing.
    NSView* container = buttons[0].superview.superview;
    if (!container) {
        return;
    }
    const auto x = static_cast<CGFloat>(_position->x);
    const auto y = static_cast<CGFloat>(_position->y);
    const CGFloat height = NSHeight(buttons[0].frame) + 2 * y;
    _placing = YES;
    NSRect frame = container.frame;
    frame.size.height = height;
    frame.origin.y = NSHeight(window.frame) - height;
    if (!NSEqualRects(frame, container.frame)) {
        container.frame = frame;
    }
    for (NSUInteger i = 0; i < buttons.count; ++i) {
        const NSPoint origin = NSMakePoint(x + static_cast<CGFloat>(i) * _spacing, y);
        if (!NSEqualPoints(origin, buttons[i].frame.origin)) {
            [buttons[i] setFrameOrigin:origin];
        }
    }
    _placing = NO;
}

- (NSRect)controlsFrame {
    NSWindow* window = _window;
    if (!window || (window.styleMask & NSWindowStyleMaskFullScreen)) {
        return NSZeroRect;
    }
    NSRect frame = NSZeroRect;
    for (NSButton* button in [self buttons]) {
        if (!button.hidden) {
            frame = NSUnionRect(frame, [button convertRect:button.bounds toView:nil]);
        }
    }
    // Window coordinates start at the bottom left; the content covers the window, top down.
    frame.origin.y = NSHeight(window.contentView.frame) - NSMaxY(frame);
    return frame;
}

- (void)stop {
    for (id observer in _observers) {
        [NSNotificationCenter.defaultCenter removeObserver:observer];
    }
    [_observers removeAllObjects];
}

@end

namespace elg::detail::platform::macos {

void* integrate_title_bar(void* ns_window, std::optional<Point> traffic_lights) {
    NSWindow* window = (__bridge NSWindow*)ns_window;
    // Keep the content size: the window was made with room for a title bar above the content,
    // which the content now covers.
    const CGFloat content_height = NSHeight([window contentRectForFrameRect:window.frame]);
    ElgTitleBar* title_bar = [[ElgTitleBar alloc] initWithWindow:window position:traffic_lights];
    [title_bar integrate];
    NSRect frame = window.frame;
    const CGFloat extra = NSHeight(frame) - content_height;
    if (extra > 0) {
        frame.size.height -= extra;
        frame.origin.y += extra; // the top stays put
        [window setFrame:frame display:NO];
    }
    return (__bridge_retained void*)title_bar;
}

void refresh_title_bar(void* title_bar) {
    [(__bridge ElgTitleBar*)title_bar integrate];
}

Rect system_controls_frame(void* title_bar) {
    const NSRect frame = [(__bridge ElgTitleBar*)title_bar controlsFrame];
    if (NSIsEmptyRect(frame)) {
        return {};
    }
    return {static_cast<float>(frame.origin.x), static_cast<float>(frame.origin.y),
            static_cast<float>(frame.size.width), static_cast<float>(frame.size.height)};
}

void release_title_bar(void* title_bar) {
    ElgTitleBar* self = (__bridge_transfer ElgTitleBar*)title_bar;
    [self stop];
}

} // namespace elg::detail::platform::macos
