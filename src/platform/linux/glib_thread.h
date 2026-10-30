// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_LINUX_GLIB_THREAD_H
#define ELG_PLATFORM_LINUX_GLIB_THREAD_H

// A GLib main loop on a thread of its own, with the session bus: D-Bus calls and signals for
// notifications, the launcher and the settings portal happen there. Only with GIO
// (ELG_HAVE_GIO).

#include <gio/gio.h>

#include <functional>
#include <string>

namespace elg::detail::platform::linux_desktop {

// Runs `work` on the GLib thread (started on first use), with the session bus, or null if
// there's none. Any thread.
void on_glib_thread(std::function<void(GDBusConnection* bus)> work);

// The app's id on the desktop: SDL's app id hint if set, else the executable's name. Its
// desktop entry is "<id>.desktop".
std::string app_id();

} // namespace elg::detail::platform::linux_desktop

#endif // ELG_PLATFORM_LINUX_GLIB_THREAD_H
