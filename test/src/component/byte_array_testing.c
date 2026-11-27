/** @file byte_array_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief ByteArray model ownership, indexing and GC regression tests.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "lib/memory/allocate.h"
#include "model/access.h"
#include "model/byte_array.h"
#include "model/context.h"
#include "model/process.h"
#include "model/thread.h"
#include "vm/gc.h"
#include "vm/gc_policy.h"

#include <assert.h>

/** @brief Consume an operation's owned result. */
static void success(operation_result_t r) {
    assert(!r.is_exception);
    DECREF(r.value);
}

/** @brief Verify and consume an exception. */
static void failure(operation_result_t r, object_t *expected) {
    assert(r.is_exception && r.value == expected);
    DECREF(r.value);
}

/** @brief Build an initialized object, equivalent to the builtin init chain. */
static object_t *make(process_t *p, object_t *parent) {
    object_t *o = create_user_defined_object(p, (object_array_t){&parent, 1});
    success(initialize_byte_array(o));
    return o;
}

/** @brief C-only population, before the public append API is introduced. */
static void populate(object_t *o) {
    size_t before = gc_policy_object_bytes(o);
    assert(byte_buffer_resize(get_byte_array_storage(o), 3, 255));
    gc_policy_note_growth(o, before);
    field_cell_t *cell = get_own_field_cell(o, get_string_length());
    DECREF(cell->object);
    cell->object = create_integer_object(o->process, 3);
}

int main(void) {
    size_t baseline = get_allocated_memory_size();
    process_t *p = create_process();
    object_t *a = make(p, get_byte_array_proto());
    gc_root_handle_t root = register_gc_root(p, a);
    assert(!get_object_boolean_value(a));
    populate(a);
    assert(get_object_boolean_value(a));
    success(initialize_byte_array(a));
    assert(get_byte_array_storage(a)->length == 3);
    object_t *child = make(p, a);
    assert(get_byte_array_storage(child)->length == 0);
    assert(get_byte_array_storage(child) != get_byte_array_storage(a));
    object_t *copy = clone_object(p, a);
    object_t *flat = flat_object_with_access(p, a, &p->main_thread->context->access);
    assert(get_byte_array_storage(copy)->data != get_byte_array_storage(a)->data);
    assert(get_byte_array_storage(flat)->data != get_byte_array_storage(a)->data);
    object_t *zero = create_integer_object(p, 0), *one = create_integer_object(p, 1);
    object_array_t indices = {&zero, 1};
    success(byte_array_set_index(p, a, indices, one));
    operation_result_t read = byte_array_get_index(p, a, indices);
    assert(get_object_integer_value(read.value).value == 1);
    success(read);
    assert(get_byte_array_storage(copy)->data[0] == 255);
    assert(get_byte_array_storage(flat)->data[0] == 255);
    object_t *bad[] = {create_integer_object(p, -1),
                       create_integer_object(p, 3),
                       create_integer_object(p, INT64_MAX),
                       create_real_number_object(p, 0.0),
                       get_null_object(),
                       get_boolean_object(false)};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
        failure(byte_array_get_index(p, a, (object_array_t){&bad[i], 1}),
                get_exception_invalid_argument());
        failure(byte_array_set_index(p, a, (object_array_t){&bad[i], 1}, one),
                get_exception_invalid_argument());
        DECREF(bad[i]);
    }
    object_t *values[] = {create_integer_object(p, -1),
                          create_integer_object(p, 256),
                          create_real_number_object(p, 1.0),
                          get_null_object(),
                          get_boolean_object(true)};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); i++) {
        failure(byte_array_set_index(p, a, indices, values[i]), get_exception_invalid_argument());
        DECREF(values[i]);
    }
    failure(byte_array_get_index(p, a, (object_array_t){NULL, 0}),
            get_exception_invalid_argument());
    failure(byte_array_set_index(p, get_byte_array_proto(), indices, one),
            get_exception_immutable_object());
    failure(initialize_byte_array(get_byte_array_proto()), get_exception_immutable_object());
    assert(get_byte_array_storage(a)->data[0] == 1 && get_byte_array_storage(a)->length == 3);
    DECREF(zero);
    DECREF(one);
    DECREF(child);
    DECREF(copy);
    DECREF(flat);
    collect_garbage(p);
    assert(get_byte_array_storage(a)->data[0] == 1);
    unregister_gc_root(p, root);
    DECREF(a);
    object_t *parent = get_root_object();
    object_t *plain = create_user_defined_object(p, (object_array_t){&parent, 1});
    assert(!get_byte_array_storage(plain));
    assert(byte_array_property_index(plain, false));
    DECREF(plain);
    a = make(p, get_byte_array_proto());
    populate(a);
    object_t *key = create_string_object(p, STATIC_STRING(U"cycle"));
    assert(create_object_property(a, key, a, false) == MSTAT_OK);
    DECREF(key);
    DECREF(a);
    collect_garbage(p);
    destroy_process(p);
    assert(get_allocated_memory_size() == baseline);
    return 0;
}
