// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Notifications on Linux (elg/desktop.h): the desktop's notification server
// (org.freedesktop.Notifications on the session bus), through GIO's D-Bus. Its signals (a click)
// arrive on a GLib main loop of our own, on its own thread. Without GIO in the build, they're
// logged.

#include "platform/generic/native.h"

#include "core/log.h"

#if defined(ELG_HAVE_GIO)

#include "core/ui_tasks.h"

#include <gio/gio.h>

#include <filesystem>
#include <map>
#include <mutex>
#include <thread>

namespace elg::detail::platform::native {

namespace {

// The executable's name, as the app's name in the notification.
std::string app_name() {
    std::error_code error;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::string("Elegance") : exe.filename().string();
}

// Talks to the notification server from its own thread and main loop. Started on first use.
class Notifier {
public:
    static Notifier& instance() {
        static Notifier notifier;
        return notifier;
    }

    // UI thread.
    void show(const Notification& notification) {
        auto* request = new Request{this, notification.title, notification.body, notification.on_click};
        g_main_context_invoke(context_, &Notifier::send, request);
    }

    ~Notifier() {
        g_main_loop_quit(loop_);
        if (thread_.joinable()) {
            thread_.join();
        }
        g_main_loop_unref(loop_);
        g_main_context_unref(context_);
    }

private:
    struct Request {
        Notifier* self;
        std::string title;
        std::string body;
        std::function<void()> on_click;
    };

    Notifier() : context_(g_main_context_new()), loop_(g_main_loop_new(context_, FALSE)), name_(app_name()) {
        thread_ = std::thread([this] {
            g_main_context_push_thread_default(context_);
            GError* error = nullptr;
            bus_ = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
            if (bus_) {
                // Subscribed here, so the signals come to this thread's context.
                subscription_ = g_dbus_connection_signal_subscribe(
                    bus_, "org.freedesktop.Notifications", "org.freedesktop.Notifications", nullptr,
                    "/org/freedesktop/Notifications", nullptr, G_DBUS_SIGNAL_FLAGS_NONE, &Notifier::on_signal, this, nullptr);
            } else {
                log_warning("notify: no session bus: {}", error ? error->message : "unknown error");
                g_clear_error(&error);
            }
            g_main_loop_run(loop_);
            if (bus_) {
                g_dbus_connection_signal_unsubscribe(bus_, subscription_);
                g_object_unref(bus_);
            }
            g_main_context_pop_thread_default(context_);
        });
    }

    // Notifier thread.
    static gboolean send(gpointer data) {
        auto* request = static_cast<Request*>(data);
        Notifier& self = *request->self;
        if (!self.bus_) {
            log_info("notification (not shown: no session bus): {}: {}", request->title, request->body);
            delete request;
            return G_SOURCE_REMOVE;
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
        g_dbus_connection_call(self.bus_, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                               "org.freedesktop.Notifications", "Notify",
                               g_variant_new("(susssasa{sv}i)", self.name_.c_str(), 0u, "", request->title.c_str(),
                                             request->body.c_str(), &actions, &hints, -1),
                               G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &Notifier::sent, request);
        return G_SOURCE_REMOVE;
    }

    // Notifier thread: the server's answer, with the notification's id.
    static void sent(GObject* bus, GAsyncResult* result, gpointer data) {
        auto* request = static_cast<Request*>(data);
        GError* error = nullptr;
        if (GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(bus), result, &error)) {
            guint32 id = 0;
            g_variant_get(reply, "(u)", &id);
            g_variant_unref(reply);
            if (request->on_click) {
                std::lock_guard lock(request->self->mutex_);
                request->self->clicks_[id] = std::move(request->on_click);
            }
        } else {
            log_warning("notify: the notification server refused it: {}", error ? error->message : "unknown error");
            g_clear_error(&error);
        }
        delete request;
    }

    // Notifier thread.
    static void on_signal(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar* signal,
                          GVariant* parameters, gpointer data) {
        auto& self = *static_cast<Notifier*>(data);
        guint32 id = 0;
        std::function<void()> click;
        if (g_strcmp0(signal, "ActionInvoked") == 0) {
            const gchar* action = nullptr;
            g_variant_get(parameters, "(u&s)", &id, &action);
            std::lock_guard lock(self.mutex_);
            if (const auto found = self.clicks_.find(id); found != self.clicks_.end()) {
                if (g_strcmp0(action, "default") == 0) {
                    click = std::move(found->second);
                }
                self.clicks_.erase(found);
            }
        } else if (g_strcmp0(signal, "NotificationClosed") == 0) {
            guint32 reason = 0;
            g_variant_get(parameters, "(uu)", &id, &reason);
            std::lock_guard lock(self.mutex_);
            self.clicks_.erase(id);
        }
        if (click) {
            post_ui_task(std::move(click));
        }
    }

    GMainContext* context_;
    GMainLoop* loop_;
    const std::string name_;
    std::thread thread_;
    GDBusConnection* bus_ = nullptr; // notifier thread
    guint subscription_ = 0;
    std::mutex mutex_;
    std::map<guint32, std::function<void()>> clicks_;
};

} // namespace

void show_notification(const Notification& notification) {
    Notifier::instance().show(notification);
}

} // namespace elg::detail::platform::native

#else

namespace elg::detail::platform::native {

void show_notification(const Notification& notification) {
    log_info("notification (not shown: this build has no D-Bus): {}: {}", notification.title, notification.body);
}

} // namespace elg::detail::platform::native

#endif
