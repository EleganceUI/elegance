// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// Links against the C library as it ships, the way a binding does, and calls into it: CI builds and
// runs it, so a package that doesn't link fails the build (ELGSPEC-0006.R5). tools/c-package-check.py
// also builds it against the installed packages, with the compiler's C++ standard library and with
// another one: it throws and catches an exception with its own, and has the library fail a call,
// which it does with an exception of the library's own C++ standard library.

#include <elg-c/elegance.h>

#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

void ignore_message(void*, const char*, size_t) {}

} // namespace

int main() {
    const char* version = elg_version();
    if (version == nullptr) {
        return 1;
    }
    try {
        throw std::runtime_error(std::string("the program's own exception"));
    } catch (const std::exception& error) {
        if (std::string(error.what()).empty()) {
            return 1;
        }
    }
    // A custom animation needs a curve: without one, the call fails with a usage error.
    if (elg_animation_custom(ElgClosure{nullptr, nullptr, nullptr}, 1.0) != nullptr ||
        elg_take_error(ignore_message, nullptr) != ELG_USAGE_ERROR) {
        return 1;
    }
    std::printf("Elegance C library %s\n", version);
    return 0;
}
