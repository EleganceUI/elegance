// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include "platform/input_apple.h"

#import <AppKit/AppKit.h>

namespace elg::detail::platform::apple {

namespace {

GesturePhase phase(NSEventPhase phase) {
    switch (phase) {
    case NSEventPhaseMayBegin: // touching down, not moving yet
    case NSEventPhaseBegan: return GesturePhase::began;
    case NSEventPhaseChanged: return GesturePhase::changed;
    case NSEventPhaseEnded: return GesturePhase::ended;
    case NSEventPhaseCancelled: return GesturePhase::cancelled;
    default: return GesturePhase::none;
    }
}

} // namespace

void* install_input_monitor(std::function<bool(const NativeInput&)> handle) {
    const NSEventMask mask =
        NSEventMaskScrollWheel | NSEventMaskMagnify | NSEventMaskRotate | NSEventMaskSmartMagnify;
    id monitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask
                                                       handler:^NSEvent*(NSEvent* event) {
        NSWindow* window = event.window;
        NSPoint location = event.locationInWindow;
        if (!window) {
            // Events without a window (posted ones) are in screen coordinates: find the app's
            // window there.
            for (NSWindow* candidate in NSApp.orderedWindows) {
                if (candidate.isVisible && NSPointInRect(location, candidate.frame)) {
                    window = candidate;
                    location = [window convertPointFromScreen:location];
                    break;
                }
            }
        }
        NSView* content = window.contentView;
        if (!content) {
            return event;
        }
        NativeInput input;
        input.ns_window = (__bridge void*)window;
        input.time = event.timestamp;
        const NSPoint point = [content convertPoint:location fromView:nil];
        input.x = static_cast<float>(point.x);
        input.y = static_cast<float>(content.isFlipped ? point.y : NSHeight(content.bounds) - point.y);
        switch (event.type) {
        case NSEventTypeScrollWheel:
            // Resting fingers send these between movements; nothing moves.
            if (event.phase == NSEventPhaseStationary) {
                return nil;
            }
            input.kind = NativeInput::Kind::scroll;
            // scrollingDelta already follows the natural scrolling setting; it's positive toward
            // the start of the content.
            input.dx = static_cast<float>(-event.scrollingDeltaX);
            input.dy = static_cast<float>(-event.scrollingDeltaY);
            input.precise = event.hasPreciseScrollingDeltas;
            if (event.momentumPhase != NSEventPhaseNone) {
                input.phase = phase(event.momentumPhase);
                input.momentum = true;
            } else {
                input.phase = phase(event.phase);
            }
            break;
        case NSEventTypeMagnify:
            input.kind = NativeInput::Kind::magnify;
            input.amount = static_cast<float>(event.magnification);
            input.phase = phase(event.phase);
            break;
        case NSEventTypeRotate:
            input.kind = NativeInput::Kind::rotate;
            input.amount = -event.rotation; // AppKit's is counterclockwise
            input.phase = phase(event.phase);
            break;
        case NSEventTypeSmartMagnify: input.kind = NativeInput::Kind::smart_magnify; break;
        default: return event;
        }
        return handle(input) ? nil : event;
    }];
    return (__bridge_retained void*)monitor;
}

void remove_input_monitor(void* monitor) {
    [NSEvent removeMonitor:(__bridge_transfer id)monitor];
}

} // namespace elg::detail::platform::apple
