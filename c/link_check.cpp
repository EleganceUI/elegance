// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Links against the C library as it ships, the way a binding does, and calls into it: CI builds and
// runs it, so a package that doesn't link fails the build (ELGSPEC-0006.R5).

#include <cstdio>

extern "C" const char* elg_version();

int main() {
    const char* version = elg_version();
    if (version == nullptr) {
        return 1;
    }
    std::printf("Elegance C library %s\n", version);
    return 0;
}
