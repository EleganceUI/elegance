// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Sandbox extensions to elg::View (design.md §9.4). This file is included inside the View
// class body, so it holds member declarations only; definitions are in view_extensions.cpp.

// A padded, rounded "card" surface with a hairline border.
View card(float corner_radius = 16) const;
