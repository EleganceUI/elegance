// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Notifications on Linux (elg/desktop.h): the desktop's notification server
// (org.freedesktop.Notifications on the session bus), through GIO's D-Bus on the GLib thread
// (glib_thread.h), where its signals (a click) arrive too. Without GIO in the build, they're
// logged.

#include "platform/generic/native.h"

#include "core/log.h"

#if defined(ELG_HAVE_GIO)

#include "core/ui_tasks.h"
#include "platform/linux/glib_thread.h"

#include <map>

namespace elg::detail::platform::native {

namespace {

using linux_desktop::on_glib_thread;

// GLib thread.
guint g_subscription = 0;
std::map<guint32, std::function<void()>> g_clicks; // by notification id

struct Request {
    std::string title;
    std::string body;
    std::function<void()> on_click;
};

void on_signal(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* signal, GVariant* parameters,
               gpointer) {
    guint32 id = 0;
    std::function<void()> click;
    if (g_strcmp0(signal, "ActionInvoked") == 0) {
        const gchar* action = nullptr;
        g_variant_get(parameters, "(u&s)", &id, &action);
        if (const auto found = g_clicks.find(id); found != g_clicks.end()) {
            if (g_strcmp0(action, "default") == 0) {
                click = std::move(found->second);
            }
            g_clicks.erase(found);
        }
    } else if (g_strcmp0(signal, "NotificationClosed") == 0) {
        guint32 reason = 0;
        g_variant_get(parameters, "(uu)", &id, &reason);
        g_clicks.erase(id);
    }
    if (click) {
        post_ui_task(std::move(click));
    }
}

// The server's answer, with the notification's id.
void sent(GObject* bus, GAsyncResult* result, gpointer data) {
    auto* request = static_cast<Request*>(data);
    GError* error = nullptr;
    if (GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(bus), result, &error)) {
        guint32 id = 0;
        g_variant_get(reply, "(u)", &id);
        g_variant_unref(reply);
        if (request->on_click) {
            g_clicks[id] = std::move(request->on_click);
        }
    } else {
        log_warning("notify: the notification server refused it: {}", error ? error->message : "unknown error");
        g_clear_error(&error);
    }
    delete request;
}

void send(GDBusConnection* bus, Request* request) {
    if (!bus) {
        log_info("notification (not shown: no session bus): {}: {}", request->title, request->body);
        delete request;
        return;
    }
    if (g_subscription == 0) {
        g_subscription = g_dbus_connection_signal_subscribe(
            bus, "org.freedesktop.Notifications", "org.freedesktop.Notifications", nullptr,
            "/org/freedesktop/Notifications", nullptr, G_DBUS_SIGNAL_FLAGS_NONE, &on_signal, nullptr, nullptr);
    }
    GVariantBuilder actions;
    g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
    if (request->on_click) {
        // "default": clicking the notification itself.
        g_variant_builder_add(&actions, "s", "default");
        g_variant_builder_add(&actions, "s", "");
    }
    GVariantBuilder hints;
    g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
    const std::string name = linux_desktop::app_id();
    g_variant_builder_add(&hints, "{sv}", "desktop-entry", g_variant_new_string(name.c_str()));
    g_dbus_connection_call(bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                           "org.freedesktop.Notifications", "Notify",
                           g_variant_new("(susssasa{sv}i)", name.c_str(), 0u, "", request->title.c_str(),
                                         request->body.c_str(), &actions, &hints, -1),
                           G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &sent, request);
}

} // namespace

void show_notification(const Notification& notification) {
    auto* request = new Request{notification.title, notification.body, notification.on_click};
    on_glib_thread([request](GDBusConnection* bus) { send(bus, request); });
}

} // namespace elg::detail::platform::native

#else

namespace elg::detail::platform::native {

void show_notification(const Notification& notification) {
    log_info("notification (not shown: this build has no D-Bus): {}: {}", notification.title, notification.body);
}

} // namespace elg::detail::platform::native

#endif
