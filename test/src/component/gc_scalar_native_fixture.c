/** @file gc_scalar_native_fixture.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Real scalar adapters retaining borrowed string views across a controlled GC.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "gc_scalar_native_fixture.h"

#include <assert.h>
#include <string.h>

static unsigned mode;
static gc_scalar_probe_t probe;

void GOAT_NATIVE_CALL gc_scalar_configure(unsigned selected, gc_scalar_probe_t callback) {
    mode = selected;
    probe = callback;
}

static void check_strings(const goat_native_value_v2_t *args) {
    static const gchar_t expected[] = U"left\U0001F980right";
    static const gchar_t extra[] = U"extra-value";
    assert(args[0].type == GOAT_NATIVE_STRING && args[1].type == GOAT_NATIVE_STRING);
    assert(args[0].value.string->length == sizeof(expected) / sizeof(*expected) - 1);
    assert(args[1].value.string->length == sizeof(extra) / sizeof(*extra) - 1);
    assert(!memcmp(args[0].value.string->data, expected, sizeof(expected) - sizeof(*expected)));
    assert(!memcmp(args[1].value.string->data, extra, sizeof(extra) - sizeof(*extra)));
}

static uint32_t GOAT_NATIVE_CALL invoke(uint32_t version,
                                        const goat_native_context_v2_t *context,
                                        uint32_t count,
                                        const goat_native_value_v2_t *args,
                                        goat_native_value_v2_t *result) {
    assert(version == GOAT_NATIVE_ABI_VERSION && count == 2 && probe);
    check_strings(args);
    /* Per-call native allocations must be reclaimed on success, failure and retry. */
    assert(goat_native_allocate(context, 127));
    if (mode >= 5)
        assert(goat_native_request_exit(context, 1, GOAT_NATIVE_I64, 256)
               == GOAT_NATIVE_HOST_EXCEPTION);
    probe();
    check_strings(args);
    if (mode == 5)
        return GOAT_NATIVE_HOST_EXCEPTION;
    if (mode == 3)
        return GOAT_NATIVE_BAD_REQUEST;
    if (mode == 4)
        return GOAT_NATIVE_RESOURCE_LIMIT;
    *result = args[0];
    if (mode == 1 || mode == 2) {
        goat_native_string_view_v2_t *view = goat_native_allocate(context, sizeof(*view));
        assert(view);
        *view = *args[0].value.string;
        if (mode == 1) {
            view->data += 4;
            view->length -= 4;
        } else {
            gchar_t *copy = goat_native_allocate(context, view->length * sizeof(*copy));
            assert(copy);
            memcpy(copy, view->data, view->length * sizeof(*copy));
            view->data = copy;
        }
        result->value.string = view;
    }
    return GOAT_NATIVE_OK;
}

static const uint32_t types[] = {GOAT_NATIVE_STRING};
static const goat_native_entry_v2_t entries[] = {
    {1, 1, 1, GOAT_NATIVE_STRING, types, invoke, NULL, GOAT_NATIVE_PURE}};
static const goat_native_module_v2_t module = {GOAT_NATIVE_ABI_VERSION,
                                               sizeof(goat_native_module_v2_t),
                                               sizeof(goat_native_value_v2_t),
                                               _Alignof(goat_native_value_v2_t),
                                               sizeof(goat_native_entry_v2_t),
                                               sizeof(void *),
                                               1,
                                               entries};

GOAT_NATIVE_API const goat_native_module_v2_t *GOAT_NATIVE_CALL
goat_native_query_v2(uint32_t version) {
    return version == GOAT_NATIVE_ABI_VERSION ? &module : NULL;
}
