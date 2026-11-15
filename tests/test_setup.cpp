// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// What every run of the tests needs before Catch2 starts.

#include "no_error_dialogs.h"

namespace {

// Nobody is there to close a dialog: failed checks and crashes fail the test, on stderr.
const bool g_no_dialogs = [] {
    elegance_apps::no_error_dialogs();
    return true;
}();

} // namespace
