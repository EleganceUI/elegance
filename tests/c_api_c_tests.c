// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The C API from C: its header (c/include/elg-c/elegance.h) compiled as C11, linked with the
// library together with tests/c_api_functions.c, which refers to every function it declares.

#include <elg-c/elegance.h>

#include <stdio.h>

extern const size_t elg_c_api_function_count; // tests/c_api_functions.c

int main(void) {
    if (elg_abi_version() != ELG_ABI_VERSION) {
        fprintf(stderr, "elg_abi_version() is %u, the header's ELG_ABI_VERSION %u\n", elg_abi_version(), ELG_ABI_VERSION);
        return 1;
    }
    if (elg_c_api_function_count == 0) {
        fprintf(stderr, "the header declares no functions\n");
        return 1;
    }
    printf("the C API's header declares %zu functions, all defined\n", elg_c_api_function_count);
    return 0;
}
