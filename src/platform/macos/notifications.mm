// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Notifications on macOS (elg/desktop.h): the User Notifications framework. macOS asks the
// person the first time; afterwards they're in System Settings > Notifications. It needs an app
// bundle (elg_add_application makes one); a bare executable only logs them. Compiled with ARC.

#include "platform/generic/native.h"

#include "core/log.h"
#include "core/ui_tasks.h"

#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace {

// Click handlers by notification identifier, for this run of the app.
std::mutex g_clicks_mutex;
std::map<std::string, std::function<void()>> g_clicks;

} // namespace

// Shows notifications while the app is in front too, and hears clicks.
@interface ElgNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@implementation ElgNotificationDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
    (void)center;
    (void)notification;
    completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList |
                      UNNotificationPresentationOptionSound);
}

- (void)userNotificationCenter:(UNUserNotificationCenter*)center
    didReceiveNotificationResponse:(UNNotificationResponse*)response
             withCompletionHandler:(void (^)(void))completionHandler {
    (void)center;
    if ([response.actionIdentifier isEqualToString:UNNotificationDefaultActionIdentifier]) {
        const std::string identifier = response.notification.request.identifier.UTF8String;
        std::function<void()> click;
        {
            std::lock_guard lock(g_clicks_mutex);
            if (const auto found = g_clicks.find(identifier); found != g_clicks.end()) {
                click = std::move(found->second);
                g_clicks.erase(found);
            }
        }
        if (click) {
            elg::detail::post_ui_task(std::move(click));
        }
    }
    completionHandler();
}
@end

namespace elg::detail::platform::native {

namespace {

ElgNotificationDelegate* g_delegate = nil;

NSString* ns_string(const std::string& text) {
    NSString* result = [NSString stringWithUTF8String:text.c_str()];
    return result ? result : @"";
}

} // namespace

void show_notification(const Notification& notification) {
    if (!NSBundle.mainBundle.bundleIdentifier) {
        log_info("notification (not shown: the app isn't in a bundle): {}: {}", notification.title, notification.body);
        return;
    }
    UNUserNotificationCenter* center = UNUserNotificationCenter.currentNotificationCenter;
    if (!g_delegate) {
        g_delegate = [ElgNotificationDelegate new];
        center.delegate = g_delegate;
    }

    UNMutableNotificationContent* content = [UNMutableNotificationContent new];
    content.title = ns_string(notification.title);
    content.body = ns_string(notification.body);
    content.sound = UNNotificationSound.defaultSound;
    NSString* identifier = NSUUID.UUID.UUIDString;
    if (notification.on_click) {
        std::lock_guard lock(g_clicks_mutex);
        g_clicks[identifier.UTF8String] = notification.on_click;
    }
    UNNotificationRequest* request = [UNNotificationRequest requestWithIdentifier:identifier content:content trigger:nil];

    // Asking again once answered returns the answer without showing anything.
    [center requestAuthorizationWithOptions:UNAuthorizationOptionAlert | UNAuthorizationOptionSound
                          completionHandler:^(BOOL granted, NSError* error) {
                            if (!granted) {
                                log_warning("notify: notifications are off for this app{}",
                                            error ? std::string(": ") + error.localizedDescription.UTF8String
                                                  : std::string());
                                return;
                            }
                            [center addNotificationRequest:request
                                     withCompletionHandler:^(NSError* failure) {
                                       if (failure) {
                                           log_warning("notify: {}", failure.localizedDescription.UTF8String);
                                       }
                                     }];
                          }];
}

} // namespace elg::detail::platform::native
