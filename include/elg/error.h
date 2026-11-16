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

/// Base class for errors Elegance reports to the developer (design.md §3.2). Bugs inside
/// Elegance itself are never thrown; they produce a crash report instead. Construct it (and its
/// subclasses) with a message, as std::runtime_error; what() returns it.
class ELG_API Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    ~Error() override;
};

/// A font, image, or file failed to load.
class ELG_API ResourceError : public Error {
public:
    using Error::Error;
    ~ResourceError() override;
};

/// No suitable GPU, out of GPU memory, or the device was lost and couldn't be recovered.
class ELG_API GpuError : public Error {
public:
    using Error::Error;
    ~GpuError() override;
};

/// The OS or windowing layer failed.
class ELG_API PlatformError : public Error {
public:
    using Error::Error;
    ~PlatformError() override;
};

/// The API was used incorrectly, e.g. writing a State off the UI thread.
class ELG_API UsageError : public Error {
public:
    using Error::Error;
    ~UsageError() override;
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

/// The kind of app code an exception escaped while the app ran (AppError).
enum class AppCode {
    body,             ///< a view's body, while the view was mounted
    effect,           ///< an effect, or the value of a reactive argument
    event_handler,    ///< an event handler, or a control's, menu's or shortcut's action
    task,             ///< a task posted to the UI thread
    timer,            ///< a timer's action (after(), every())
    lifecycle_action, ///< on_appear or on_disappear
    layout,           ///< a Layout, a layout modifier, or a size observer
    drawing,          ///< a Canvas's draw function
    callback,         ///< another callback, such as a language binding's
};

/// What the app's error handler decides about an exception (App::on_error).
enum class ErrorAction {
    resume, ///< the app goes on
    quit,   ///< the run ends, and App::run() throws the exception
};

} // namespace elg

#endif // ELG_ERROR_H
