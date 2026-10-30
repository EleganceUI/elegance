// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The Dock icon on macOS (elg/desktop.h): the badge is the Dock tile's label; progress is drawn
// over the app's icon by a view of our own in the tile, as Finder and Safari show downloads.
// Compiled with ARC.

#include "platform/generic/native.h"

#import <AppKit/AppKit.h>

#include <algorithm>

// The app's icon with a progress bar along its bottom.
@interface ElgDockProgressView : NSView
@property(nonatomic) double fraction;
@property(nonatomic) elg::ProgressState state;
@end

@implementation ElgDockProgressView
- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    const NSRect bounds = self.bounds;
    [NSApp.applicationIconImage drawInRect:bounds];

    const CGFloat height = bounds.size.height * 0.1;
    const NSRect track = NSMakeRect(bounds.size.width * 0.1, bounds.size.height * 0.08, bounds.size.width * 0.8, height);
    [[NSColor colorWithWhite:0.15 alpha:0.75] setFill];
    [[NSBezierPath bezierPathWithRoundedRect:track xRadius:height / 2 yRadius:height / 2] fill];

    const bool full = self.state == elg::ProgressState::indeterminate;
    NSRect bar = NSInsetRect(track, height * 0.15, height * 0.15);
    bar.size.width *= full ? 1.0 : std::clamp(self.fraction, 0.0, 1.0);
    if (bar.size.width <= 0) {
        return;
    }
    NSColor* fill = self.state == elg::ProgressState::error    ? NSColor.systemRedColor
                    : self.state == elg::ProgressState::paused ? NSColor.systemYellowColor
                                                               : NSColor.controlAccentColor;
    [fill setFill];
    const CGFloat radius = bar.size.height / 2;
    [[NSBezierPath bezierPathWithRoundedRect:bar xRadius:radius yRadius:radius] fill];
}
@end

namespace elg::detail::platform::native {

void set_badge(const std::optional<std::string>& text) {
    NSApp.dockTile.badgeLabel = text ? [NSString stringWithUTF8String:text->c_str()] : nil;
}

void set_progress(std::optional<double> fraction, ProgressState state) {
    NSDockTile* tile = NSApp.dockTile;
    if (!fraction) {
        tile.contentView = nil; // the plain icon again
    } else {
        auto* view = [tile.contentView isKindOfClass:ElgDockProgressView.class]
                         ? static_cast<ElgDockProgressView*>(tile.contentView)
                         : [[ElgDockProgressView alloc] initWithFrame:NSMakeRect(0, 0, tile.size.width, tile.size.height)];
        view.fraction = fraction.value_or(0);
        view.state = state;
        tile.contentView = view;
    }
    [tile display];
}

} // namespace elg::detail::platform::native
