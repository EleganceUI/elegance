// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#include <elg/error.h>

namespace elg {

// Out-of-line destructors anchor each class's vtable and type info in the library, so catching
// these types works across the DLL boundary on every platform.
Error::~Error() = default;
ResourceError::~ResourceError() = default;
GpuError::~GpuError() = default;
PlatformError::~PlatformError() = default;
UsageError::~UsageError() = default;

} // namespace elg
