// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// A views module that hot_reload_tests.cpp loads, with a root view and one preview.

#include <elg/hot_reload.h>
#include <elg/text.h>

using namespace elg;

namespace {

View Root() {
    return Text("second root");
}

} // namespace

ELG_MODULE(Root)

ELG_PREVIEW("second: one") {
    return Text("second: one");
}
