// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_WINDOWS_APP_ID_H
#define ELG_PLATFORM_WINDOWS_APP_ID_H

#include <string>

namespace elg::detail::platform::windows {

// The AppUserModelID the app's toasts show under (notifications.cpp). Windows shows toasts only
// for an id that a Start menu shortcut carries. The id the app set for its process, if it set one
// (its installer's shortcut carries it); otherwise Elegance.App.<the executable's name, ASCII
// letters and digits>, after making sure the person's Start menu has a shortcut to the executable
// carrying it: <the executable's name>.lnk in their Programs folder, made or brought up to date
// here, unless a shortcut by that name is another program's (then it logs a warning). UI thread,
// with COM initialized.
std::wstring app_user_model_id();

} // namespace elg::detail::platform::windows

#endif // ELG_PLATFORM_WINDOWS_APP_ID_H
