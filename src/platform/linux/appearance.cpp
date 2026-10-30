// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Appearance on Linux (platform/generic/native.h): no window materials (compositors don't offer
// a blurred backdrop to clients). The accent color comes from the settings portal
// (org.freedesktop.appearance accent-color), read on the GLib thread and kept here; a change
// reaches the app as a system theme event. Without GIO, there's none.

#include "platform/generic/native.h"

#if defined(ELG_HAVE_GIO)

#include "platform/linux/glib_thread.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <mutex>

namespace elg::detail::platform::native {

namespace {

constexpr const char* kPortal = "org.freedesktop.portal.Desktop";
constexpr const char* kPath = "/org/freedesktop/portal/desktop";
constexpr const char* kSettings = "org.freedesktop.portal.Settings";

std::mutex g_mutex;
std::optional<Color> g_accent; // g_mutex
guint g_subscription = 0;      // GLib thread

// Stores the portal's value, (ddd) with each in [0, 1], or unset when out of range, and tells
// the app. GLib thread.
void store(GVariant* value) {
    // Older portals wrap the value in one more variant.
    GVariant* inner = g_variant_ref(value);
    while (g_variant_is_of_type(inner, G_VARIANT_TYPE_VARIANT)) {
        GVariant* next = g_variant_get_variant(inner);
        g_variant_unref(inner);
        inner = next;
    }
    std::optional<Color> accent;
    if (g_variant_is_of_type(inner, G_VARIANT_TYPE("(ddd)"))) {
        double r = 0, g = 0, b = 0;
        g_variant_get(inner, "(ddd)", &r, &g, &b);
        const auto in_range = [](double c) { return c >= 0 && c <= 1; };
        if (in_range(r) && in_range(g) && in_range(b)) {
            const auto channel = [](double c) { return static_cast<std::uint8_t>(std::lround(c * 255)); };
            accent = Color::rgb(channel(r), channel(g), channel(b));
        }
    }
    g_variant_unref(inner);
    {
        std::lock_guard lock(g_mutex);
        if (g_accent == accent) {
            return;
        }
        g_accent = accent;
    }
    SDL_Event event{};
    event.type = SDL_EVENT_SYSTEM_THEME_CHANGED;
    SDL_PushEvent(&event);
}

void on_setting_changed(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* parameters,
                        gpointer) {
    const gchar* name_space = nullptr;
    const gchar* key = nullptr;
    GVariant* value = nullptr;
    g_variant_get(parameters, "(&s&sv)", &name_space, &key, &value);
    if (g_strcmp0(name_space, "org.freedesktop.appearance") == 0 && g_strcmp0(key, "accent-color") == 0) {
        store(value);
    }
    g_variant_unref(value);
}

void read_done(GObject* bus, GAsyncResult* result, gpointer) {
    GError* error = nullptr;
    if (GVariant* reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(bus), result, &error)) {
        GVariant* value = nullptr;
        g_variant_get(reply, "(v)", &value);
        store(value);
        g_variant_unref(value);
        g_variant_unref(reply);
    } else {
        g_clear_error(&error); // no portal, or no accent color: none
    }
}

} // namespace

bool supports_material(WindowMaterial) {
    return false;
}

void appearance_changed(SDL_Window*, WindowParts&) {}

std::optional<Color> system_accent_color() {
    std::lock_guard lock(g_mutex);
    return g_accent;
}

void watch_appearance() {
    linux_desktop::on_glib_thread([](GDBusConnection* bus) {
        if (!bus || g_subscription != 0) {
            return;
        }
        g_subscription = g_dbus_connection_signal_subscribe(bus, kPortal, kSettings, "SettingChanged", kPath, nullptr,
                                                            G_DBUS_SIGNAL_FLAGS_NONE, &on_setting_changed, nullptr,
                                                            nullptr);
        g_dbus_connection_call(bus, kPortal, kPath, kSettings, "ReadOne",
                               g_variant_new("(ss)", "org.freedesktop.appearance", "accent-color"),
                               G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, &read_done, nullptr);
    });
}

void unwatch_appearance() {
    linux_desktop::on_glib_thread([](GDBusConnection* bus) {
        if (bus && g_subscription != 0) {
            g_dbus_connection_signal_unsubscribe(bus, g_subscription);
            g_subscription = 0;
        }
    });
}

} // namespace elg::detail::platform::native

#else

namespace elg::detail::platform::native {

bool supports_material(WindowMaterial) {
    return false;
}

void appearance_changed(SDL_Window*, WindowParts&) {}

std::optional<Color> system_accent_color() {
    return std::nullopt;
}

void watch_appearance() {}

void unwatch_appearance() {}

} // namespace elg::detail::platform::native

#endif
