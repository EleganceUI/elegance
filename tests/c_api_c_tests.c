// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
// If a copy of the MPL was not distributed with this file, You can obtain one at
// https://mozilla.org/MPL/2.0/.

// The C API from C: its header (c/include/elg-c/elegance.h) compiled as C11, linked with the
// library together with what tools/c-header.py writes from the header (c_api_reference.h): a
// reference to every function it declares, and the layout of every struct it declares. Then how
// the API reports errors, takes what it consumes, drops closures, fails on the wrong thread, and
// checks handles to reactive state. Run under AddressSanitizer, which also reports what isn't
// freed.

#include "c_api_c_support.h"
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
    Text error = {NULL, 0};
    CHECK(elg_take_error(append, &error) == ELG_NO_ERROR);
    free(error.data);
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

// --- Closures, counted ---

typedef struct Counts {
    int calls;
    int drops;
} Counts;

static bool count_call(void* data, void* args, void* ret) {
    (void)args;
    (void)ret;
    ++((Counts*)data)->calls;
    return true;
}

static void count_drop(void* data) {
    ++((Counts*)data)->drops;
}

static ElgClosure closure(Counts* counts) {
    const ElgClosure made = {counts, count_call, count_drop};
    return made;
}

// No closure (`call` is null), which Elegance must still drop.
static ElgClosure none(Counts* counts) {
    const ElgClosure made = {counts, NULL, count_drop};
    return made;
}

static void ignore_message(void* ctx, const char* ptr, size_t len) {
    (void)ctx;
    (void)ptr;
    (void)len;
}

// The calling thread's error kind, taken.
static int32_t take_error(void) {
    return elg_take_error(ignore_message, NULL);
}

// --- Errors ---

typedef struct Message {
    char text[512];
    size_t length;
} Message;

static void keep_message(void* ctx, const char* ptr, size_t len) {
    Message* message = (Message*)ctx;
    message->length = len < sizeof(message->text) - 1 ? len : sizeof(message->text) - 1;
    memcpy(message->text, ptr, message->length);
    message->text[message->length] = '\0';
}

// A call that fails records its error for this thread; taking it gives its kind and message, and
// clears it.
static void errors_are_recorded_until_taken(void) {
    CHECK(!elg_error_pending());
    Counts on_click = {0, 0};
    CHECK(elg_button(elg_empty_view(), none(&on_click)) == NULL); // its action is required
    CHECK(elg_error_pending());
    Message message = {"", 0};
    CHECK(elg_take_error(keep_message, &message) == ELG_USAGE_ERROR);
    CHECK(message.length > 0);
    CHECK(!elg_error_pending());
    Message none_left = {"", 0};
    CHECK(elg_take_error(keep_message, &none_left) == ELG_NO_ERROR);
    CHECK(none_left.length == 0);
}

// --- Consumed arguments ---

// A call that succeeds takes what it consumes: the label and action of a button, the children of a
// stack. Freeing the stack frees them all (AddressSanitizer reports what isn't).
static void consumed_when_the_call_succeeds(void) {
    Counts on_click = {0, 0};
    ElgView* button = elg_button(elg_empty_view(), closure(&on_click));
    CHECK(button != NULL);
    ElgView* children[2] = {button, elg_empty_view()};
    const ElgOptLen spacing = {false, 0.f};
    ElgView* stack = elg_vstack(0, spacing, children, 2);
    CHECK(stack != NULL);
    CHECK(on_click.drops == 0);
    elg_view_free(stack);
    CHECK(on_click.drops == 1);
    CHECK(on_click.calls == 0);
    CHECK(take_error() == ELG_NO_ERROR);
}

// A call that fails still takes what it consumes: its label, its closures, a prop's closure.
static void consumed_when_the_call_fails(void) {
    Counts on_click = {0, 0};
    CHECK(elg_button(elg_empty_view(), none(&on_click)) == NULL);
    CHECK(take_error() == ELG_USAGE_ERROR);
    CHECK(on_click.drops == 1);

    Counts title = {0, 0};
    Counts action = {0, 0};
    const ElgProp reactive_title = {1, 0, NULL, closure(&title)};
    CHECK(elg_button_title(reactive_title, none(&action)) == NULL);
    CHECK(take_error() == ELG_USAGE_ERROR);
    CHECK(title.drops == 1);
    CHECK(title.calls == 0);
    CHECK(action.drops == 1);
}

// --- Closures ---

// A closure with a null `call` is no closure: where one is required, the call fails with a usage
// error; where it's optional, there's none. Either way, it's dropped.
static void closures_with_a_null_call(void) {
    Counts curve = {0, 0};
    CHECK(elg_animation_custom(none(&curve), 1.0) == NULL);
    CHECK(take_error() == ELG_USAGE_ERROR);
    CHECK(curve.drops == 1);

    ElgApp* app = elg_app_new();
    CHECK(app != NULL);
    Counts handler = {0, 0};
    elg_app_on_error(app, none(&handler)); // no error handler
    CHECK(take_error() == ELG_NO_ERROR);
    CHECK(handler.drops == 1);
    elg_app_free(app);
}

// Each closure is dropped exactly once: when what keeps it goes, right after a call that only
// calls it during the call, and at once when nothing keeps it.
static void closures_are_dropped_once(void) {
    Counts work = {0, 0};
    elg_untrack(closure(&work));
    CHECK(work.calls == 1);
    CHECK(work.drops == 1);

    Counts condition = {0, 0};
    const bool shown = true;
    const ElgProp static_condition = {0, 0, &shown, closure(&condition)}; // a static prop: its closure isn't used
    ElgView* view = elg_if(static_condition, elg_empty_view(), elg_empty_view());
    CHECK(view != NULL);
    CHECK(condition.drops == 1);
    elg_view_free(view);
    CHECK(condition.drops == 1);
    CHECK(condition.calls == 0);

    Counts first = {0, 0};
    Counts second = {0, 0};
    ElgApp* app = elg_app_new();
    elg_app_on_error(app, closure(&first));
    elg_app_on_error(app, closure(&second)); // replaces the first
    CHECK(first.drops == 1);
    CHECK(second.drops == 0);
    elg_app_free(app);
    CHECK(second.drops == 1);
    CHECK(first.calls == 0 && second.calls == 0);
    CHECK(take_error() == ELG_NO_ERROR);
}

// --- Threads ---

// What a call on another thread saw.
typedef struct OtherThread {
    ElgView* label;   // made on the UI thread, given to elg_button() there
    Counts* on_click; // the action given with it
    ElgView** children;
    bool view_made;
    int32_t view_error;
    Message view_message;
    bool button_made;
    int32_t button_error;
    int drops_there;
    bool stack_made;
    int32_t stack_error;
    int32_t check_error;
    bool feed_made;
    bool version_read;
    int32_t any_thread_error;
} OtherThread;

static void call_from_another_thread(void* context) {
    OtherThread* seen = (OtherThread*)context;
    seen->view_made = elg_empty_view() != NULL;
    seen->view_error = elg_take_error(keep_message, &seen->view_message);
    seen->button_made = elg_button(seen->label, closure(seen->on_click)) != NULL;
    seen->button_error = take_error();
    seen->drops_there = seen->on_click->drops;
    const ElgOptLen spacing = {false, 0.f};
    seen->stack_made = elg_vstack(0, spacing, seen->children, 2) != NULL;
    seen->stack_error = take_error();
    elg_check_ui_thread();
    seen->check_error = take_error();
    ElgVideoFeed* feed = elg_video_feed_new(); // callable from any thread, as are the next two
    seen->feed_made = feed != NULL;
    elg_video_feed_free(feed);
    seen->version_read = elg_version() != NULL;
    seen->any_thread_error = take_error();
}

// While an App runs, a function of the UI thread called on another thread fails with a usage
// error, and what it consumes is released on the UI thread; a function callable from any thread
// works there.
static void calls_from_the_wrong_thread(void) {
    elg_test_begin_ui_thread();
    Counts on_click = {0, 0};
    ElgView* children[2] = {elg_empty_view(), elg_empty_view()};
    OtherThread seen;
    memset(&seen, 0, sizeof(seen));
    seen.label = elg_empty_view();
    seen.on_click = &on_click;
    seen.children = children;
    elg_test_on_another_thread(call_from_another_thread, &seen);
    CHECK(!seen.view_made);
    CHECK(seen.view_error == ELG_USAGE_ERROR);
    CHECK(strstr(seen.view_message.text, "elg_empty_view() was called off the UI thread") == seen.view_message.text);
    CHECK(!seen.button_made);
    CHECK(seen.button_error == ELG_USAGE_ERROR);
    CHECK(seen.drops_there == 0); // what it consumed waits for the UI thread
    CHECK(!seen.stack_made);
    CHECK(seen.stack_error == ELG_USAGE_ERROR);
    CHECK(seen.check_error == ELG_USAGE_ERROR);
    CHECK(seen.feed_made);
    CHECK(seen.version_read);
    CHECK(seen.any_thread_error == ELG_NO_ERROR);
    CHECK(on_click.drops == 0);
    CHECK(elg_test_run_ui_releases());
    CHECK(on_click.drops == 1); // and the label, and the children (AddressSanitizer reports what isn't)
    elg_test_end_ui_thread();
}

// --- Reactive handles ---

typedef struct Signal {
    Counts* owner;
    uint64_t handle;
    int32_t error;
    uint64_t ended; // a handle whose node ended before this one was made
    int32_t ended_error;
} Signal;

static void make_signal(void* context) {
    Signal* signal = (Signal*)context;
    signal->handle = elg_signal_new(closure(signal->owner));
    elg_node_notify(signal->handle);
    elg_node_track(signal->handle);
    signal->error = take_error();
    if (signal->ended) {
        elg_node_track(signal->ended);
        signal->ended_error = take_error();
    }
}

// A handle to a signal whose node has ended (with its scope) fails with a usage error, also once
// another node has the slot it had.
static void handles_to_ended_state(void) {
    Counts first_owner = {0, 0};
    Signal first = {&first_owner, 0, -1, 0, -1};
    elg_test_in_scope(make_signal, &first);
    CHECK(first.handle != 0);
    CHECK(first.error == ELG_NO_ERROR);
    CHECK(first_owner.drops == 1); // the node went with its scope
    elg_node_track(first.handle);
    CHECK(take_error() == ELG_USAGE_ERROR);
    elg_node_notify(first.handle);
    CHECK(take_error() == ELG_USAGE_ERROR);

    Counts second_owner = {0, 0};
    Signal second = {&second_owner, 0, -1, first.handle, -1};
    elg_test_in_scope(make_signal, &second);
    CHECK(second.handle != 0);
    CHECK(second.handle != first.handle);
    CHECK(second.error == ELG_NO_ERROR);
    CHECK(second.ended_error == ELG_USAGE_ERROR);

    elg_node_track(0); // no handle at all
    CHECK(take_error() == ELG_USAGE_ERROR);
}

int main(void) {
    CHECK(elg_abi_version() == ELG_ABI_VERSION);
    CHECK(elg_c_api_function_count > 0);
    layouts_match();
    errors_are_recorded_until_taken();
    consumed_when_the_call_succeeds();
    consumed_when_the_call_fails();
    closures_with_a_null_call();
    closures_are_dropped_once();
    calls_from_the_wrong_thread();
    handles_to_ended_state();
    if (failures) {
        fprintf(stderr, "%d C API checks failed\n", failures);
        return 1;
    }
    printf("the C API's header declares %zu functions, all defined, and %zu structs, laid out as the library's; "
           "the C tests passed\n",
           elg_c_api_function_count, elg_c_api_layout_count);
    return 0;
}
