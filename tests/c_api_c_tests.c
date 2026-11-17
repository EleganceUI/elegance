// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The C API from C: its header (c/include/elg-c/elegance.h) compiled as C11, linked with the
// library together with what tools/c-header.py writes from the header (c_api_reference.h): a
// reference to every function it declares, and the layout of every struct it declares.

#include "c_api_reference.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition);                \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

// --- Layouts ---

// What the elg_layouts_* functions report: their lines, one after the other.
typedef struct Text {
    char* data;
    size_t length;
} Text;

static void append(void* ctx, const char* ptr, size_t len) {
    Text* text = (Text*)ctx;
    char* grown = (char*)realloc(text->data, text->length + len + 1);
    if (!grown) {
        abort();
    }
    memcpy(grown + text->length, ptr, len);
    text->length += len;
    grown[text->length] = '\0';
    text->data = grown;
}

static const ElgLayoutReference* reference(const char* name) {
    for (size_t i = 0; i < elg_c_api_layout_count; ++i) {
        if (strcmp(elg_c_api_layouts[i].name, name) == 0) {
            return &elg_c_api_layouts[i];
        }
    }
    return NULL;
}

// The next word of `*cursor` (words are separated by single spaces), ended in place; NULL after the
// last.
static char* next_word(char** cursor) {
    char* word = *cursor;
    if (!word || !*word) {
        return NULL;
    }
    char* space = strchr(word, ' ');
    if (space) {
        *space = '\0';
    }
    *cursor = space ? space + 1 : NULL;
    return word;
}

// Compares one reported line ("name size align field:offset ...") with the header's layout.
static void check_line(char* line) {
    char* cursor = line;
    const char* name = next_word(&cursor);
    const char* size = next_word(&cursor);
    const char* align = next_word(&cursor);
    const ElgLayoutReference* expected = name ? reference(name) : NULL;
    if (!expected || !size || !align) {
        fprintf(stderr, "the library reports a struct the header doesn't declare: %s\n", name ? name : "(none)");
        ++failures;
        return;
    }
    if (strtoul(size, NULL, 10) != expected->size || strtoul(align, NULL, 10) != expected->align) {
        fprintf(stderr, "%s: the library's size and alignment are %s and %s, the header's %zu and %zu\n", name, size,
                align, expected->size, expected->align);
        ++failures;
    }
    size_t fields = 0;
    for (char* field = next_word(&cursor); field; field = next_word(&cursor), ++fields) {
        char* colon = strchr(field, ':');
        if (!colon || fields >= expected->field_count) {
            fprintf(stderr, "%s: the library reports a field the header doesn't declare: %s\n", name, field);
            ++failures;
            return;
        }
        *colon = '\0';
        const ElgFieldReference* declared = &expected->fields[fields];
        if (strcmp(field, declared->name) != 0 || strtoul(colon + 1, NULL, 10) != declared->offset) {
            fprintf(stderr, "%s: the library's field %zu is %s at %s, the header's %s at %zu\n", name, fields, field,
                    colon + 1, declared->name, declared->offset);
            ++failures;
        }
    }
    if (fields != expected->field_count) {
        fprintf(stderr, "%s: the library reports %zu fields, the header declares %zu\n", name, fields,
                expected->field_count);
        ++failures;
    }
}

// Every struct the library reports has the layout the header's declaration has, and the library
// reports every struct the header declares.
static void layouts_match(void) {
    Text text = {NULL, 0};
    elg_layouts_core(append, &text);
    elg_layouts_views(append, &text);
    elg_layouts_ext(append, &text);
    elg_layouts_platform(append, &text);
    elg_layouts_styles(append, &text);
    CHECK(elg_take_error(NULL, NULL) == ELG_NO_ERROR);
    size_t lines = 0;
    for (char* line = text.data; line && *line;) {
        char* end = strchr(line, '\n');
        if (end) {
            *end = '\0';
        }
        check_line(line);
        ++lines;
        line = end ? end + 1 : NULL;
    }
    if (lines != elg_c_api_layout_count) {
        fprintf(stderr, "the library reports %zu structs, the header declares %zu\n", lines, elg_c_api_layout_count);
        ++failures;
    }
    free(text.data);
}

int main(void) {
    CHECK(elg_abi_version() == ELG_ABI_VERSION);
    CHECK(elg_c_api_function_count > 0);
    layouts_match();
    if (failures) {
        fprintf(stderr, "%d C API checks failed\n", failures);
        return 1;
    }
    printf("the C API's header declares %zu functions, all defined, and %zu structs, laid out as the library's\n",
           elg_c_api_function_count, elg_c_api_layout_count);
    return 0;
}
