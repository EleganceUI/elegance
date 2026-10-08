// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_ERROR_H
#define ELG_ERROR_H

#include <elg/export.h>

#include <stdexcept>

namespace elg {

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4275) // std::runtime_error is part of the C++ runtime, not our DLL
#endif

// Base class for errors Elegence reports to the developer (design.md §3.2). Bugs inside
// Elegence itself are never thrown; they produce a crash report instead.
class ELG_API Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    ~Error() override;
};

// A font, image, or file failed to load.
class ELG_API ResourceError : public Error {
public:
    using Error::Error;
    ~ResourceError() override;
};

// No suitable GPU, out of GPU memory, or the device was lost and couldn't be recovered.
class ELG_API GpuError : public Error {
public:
    using Error::Error;
    ~GpuError() override;
};

// The OS or windowing layer failed.
class ELG_API PlatformError : public Error {
public:
    using Error::Error;
    ~PlatformError() override;
};

// The API was used incorrectly.
class ELG_API UsageError : public Error {
public:
    using Error::Error;
    ~UsageError() override;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

} // namespace elg

#endif // ELG_ERROR_H
