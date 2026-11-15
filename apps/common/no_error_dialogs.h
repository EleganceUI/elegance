// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_APPS_COMMON_NO_ERROR_DIALOGS_H
#define ELG_APPS_COMMON_NO_ERROR_DIALOGS_H

// For runs nobody watches, such as CI's tests and smoke tests. On Windows, a failed check of the
// debug runtime (an assert, an invalid iterator), abort() and a crash otherwise show a dialog that
// waits for someone to close it, and the run hangs. With this, they print to stderr and end the
// process with a failure instead. Elsewhere it does nothing.

#if defined(_WIN32)
#include <windows.h>

#include <crtdbg.h>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#endif

namespace elegance_apps {

inline void no_error_dialogs() {
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG) // the debug runtime's reports; macros that do nothing otherwise
    for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
    // An assert or an error ends the run, failed, where it would have gone on once reported.
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, [](int type, char* message, int*) -> int {
        if (type == _CRT_WARN) {
            return FALSE; // reported to stderr as set above, and the run goes on
        }
        std::fputs(message, stderr);
        std::fflush(stderr);
        std::_Exit(3);
    });
#endif
#endif
}

} // namespace elegance_apps

#endif // ELG_APPS_COMMON_NO_ERROR_DIALOGS_H
