// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// A views module that hot_reload_tests.cpp loads, with a root view and two previews.

#include "probe.h"

#include <elg/hot_reload.h>
#include <elg/text.h>

using namespace elg;

namespace {

View Root() {
    return elg_tests::ProbedRoot{"first root"};
}

} // namespace

ELG_MODULE(Root)

ELG_PREVIEW("first: one") {
    return Text("first: one");
}

ELG_PREVIEW("first: two") {
    return Text("first: two");
}
