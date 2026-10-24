// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef SANDBOX_CONTACTS_H
#define SANDBOX_CONTACTS_H

#include <elg/elegance.h>

// A List of 10,000 contacts with a live filter: exercises lazy stacks.
elg::View ContactsWindow(elg::State<elg::ColorScheme> scheme);

#endif // SANDBOX_CONTACTS_H
