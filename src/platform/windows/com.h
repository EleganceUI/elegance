// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_PLATFORM_WINDOWS_COM_H
#define ELG_PLATFORM_WINDOWS_COM_H

// COM for Elegance's code on Windows (ELGSPEC-0019), on threads Elegance may not have started.

#include <windows.h>

namespace elg::detail::os {

// Lets a thread on which nothing set COM up use it anyway, in the multithreaded apartment, which
// this keeps for the rest of the process (CoIncrementMTAUsage, the first time): Elegance doesn't
// set COM up on a thread it didn't start (ELGSPEC-0019.R1). A thread that the app or Elegance set
// up keeps its own apartment. Returns how keeping it went (S_OK once kept). Thread-safe.
// Implemented in platform/windows/os.cpp.
HRESULT keep_multithreaded_apartment() noexcept;

} // namespace elg::detail::os

#endif // ELG_PLATFORM_WINDOWS_COM_H
