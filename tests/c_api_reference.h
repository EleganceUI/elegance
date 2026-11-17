// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// What tools/c-header.py writes into tests/c_api_functions.c and tests/c_api_layouts.c, from the
// C API's header: every function it declares, and every struct's layout as a C compiler sees it.

#ifndef ELG_TESTS_C_API_REFERENCE_H
#define ELG_TESTS_C_API_REFERENCE_H

#include <elg-c/elegance.h>

#include <stddef.h>

typedef void (*ElgAnyFunction)(void);

extern const ElgAnyFunction elg_c_api_functions[];
extern const size_t elg_c_api_function_count;

// A field of a struct the header declares: its name and offset.
typedef struct ElgFieldReference {
    const char* name;
    size_t offset;
} ElgFieldReference;

// A struct the header declares: its name, size, alignment and fields.
typedef struct ElgLayoutReference {
    const char* name;
    size_t size;
    size_t align;
    const ElgFieldReference* fields;
    size_t field_count;
} ElgLayoutReference;

extern const ElgLayoutReference elg_c_api_layouts[];
extern const size_t elg_c_api_layout_count;

#endif // ELG_TESTS_C_API_REFERENCE_H
