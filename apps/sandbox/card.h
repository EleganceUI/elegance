// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

#ifndef ELG_SANDBOX_CARD_H
#define ELG_SANDBOX_CARD_H

#include <elg/elegance.h>

namespace sandbox {

// A padded, rounded "card" surface with a hairline border, applied in a chain with
// View::apply(): Text("Hi").padding(8).apply(card).
[[nodiscard]] elg::View card(const elg::View& content);

} // namespace sandbox

#endif // ELG_SANDBOX_CARD_H
