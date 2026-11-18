// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// A views module that hot_reload_tests.cpp loads, whose entry says it was compiled with other
// Elegance headers, as a module built after they changed would be.

#include "probe.h"

#include <elg/hot_reload.h>

using namespace elg;

namespace {

View Root() {
    return elg_tests::ProbedRoot{"changed root"};
}

} // namespace

ELG_MODULE_EXPORT const ModuleEntry* elg_module_entry();
ELG_MODULE_EXPORT const ModuleEntry* elg_module_entry() {
    static const ModuleEntry entry = [] {
        ModuleEntry made = detail::module_entry(&Root);
        made.elegance_hash = ~detail::headers_hash;
        return made;
    }();
    return &entry;
}
