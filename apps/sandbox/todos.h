// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef SANDBOX_TODOS_H
#define SANDBOX_TODOS_H

#include <elg/reactive.h>
#include <elg/theme.h>
#include <elg/view.h>

// A small todo list: State holding a vector, ForEach keyed by id, and If.
elg::View TodoWindow(elg::State<elg::ColorScheme> scheme);

#endif // SANDBOX_TODOS_H
