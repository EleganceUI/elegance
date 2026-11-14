// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// What every run of the tests needs before Catch2 starts.

#if defined(_WIN32)

#include <windows.h>

#include <crtdbg.h>
#include <cstdlib>
#include <initializer_list>

namespace {

// Nobody is there to close a dialog in CI: a failed check of the debug runtime (an assert, an
// invalid iterator), abort() and a crash print to stderr and end the test instead of waiting.
const bool g_no_dialogs = [] {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG) // the debug runtime's reports; macros that do nothing otherwise
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
#endif
    return true;
}();

} // namespace

#endif
