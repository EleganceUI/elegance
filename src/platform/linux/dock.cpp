// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The launcher on Linux (elg/desktop.h): the Unity launcher API (com.canonical.Unity.LauncherEntry),
// which KDE Plasma, Ubuntu's dock and Dash to Dock show on the app's icon. It matches the app by
// its desktop entry (glib_thread.h's app_id). A badge shows only when it's a number. Without
// GIO, neither shows.

#include "platform/generic/native.h"

#if defined(ELG_HAVE_GIO)

#include "platform/linux/glib_thread.h"

#include <charconv>

namespace elg::detail::platform::native {

namespace {

struct Entry {
    std::optional<std::int64_t> count;
    std::optional<double> progress;
};

Entry g_entry; // UI thread

// Sends the whole entry: the launcher keeps what it was last told.
void send(Entry entry) {
    linux_desktop::on_glib_thread([entry](GDBusConnection* bus) {
        if (!bus) {
            return;
        }
        GVariantBuilder properties;
        g_variant_builder_init(&properties, G_VARIANT_TYPE("a{sv}"));
        g_variant_builder_add(&properties, "{sv}", "count", g_variant_new_int64(entry.count.value_or(0)));
        g_variant_builder_add(&properties, "{sv}", "count-visible", g_variant_new_boolean(entry.count.has_value()));
        g_variant_builder_add(&properties, "{sv}", "progress", g_variant_new_double(entry.progress.value_or(0)));
        g_variant_builder_add(&properties, "{sv}", "progress-visible", g_variant_new_boolean(entry.progress.has_value()));
        const std::string uri = "application://" + linux_desktop::app_id() + ".desktop";
        g_dbus_connection_emit_signal(bus, nullptr, "/com/canonical/unity/launcherentry/elegance",
                                      "com.canonical.Unity.LauncherEntry", "Update",
                                      g_variant_new("(sa{sv})", uri.c_str(), &properties), nullptr);
    });
}

} // namespace

void set_badge(const std::optional<std::string>& text) {
    std::optional<std::int64_t> count;
    if (text) {
        std::int64_t value = 0;
        const char* end = text->data() + text->size();
        if (const auto [rest, error] = std::from_chars(text->data(), end, value); error == std::errc() && rest == end) {
            count = value;
        }
    }
    g_entry.count = count;
    send(g_entry);
}

void set_progress(std::optional<double> fraction, ProgressState state) {
    // The launcher has no states: indeterminate shows as empty.
    g_entry.progress = fraction ? std::optional(state == ProgressState::indeterminate ? 0.0 : *fraction) : std::nullopt;
    send(g_entry);
}

} // namespace elg::detail::platform::native

#else

namespace elg::detail::platform::native {

void set_badge(const std::optional<std::string>&) {}

void set_progress(std::optional<double>, ProgressState) {}

} // namespace elg::detail::platform::native

#endif
