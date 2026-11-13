/** @file init_snapshot_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Constructor snapshot contracts before enabling automatic invocation.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "lib/memory/allocate.h"
#include "model/access.h"
#include "model/builtin_function.h"
#include "model/context.h"
#include "model/exception.h"
#include "model/init_snapshot.h"
#include "model/process.h"
#include "model/thread.h"
#include "vm/gc.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static object_t *init_key(void) {
    static object_static_string_t storage;
    return get_static_string_object(&storage, U"init");
}

static object_t *object(process_t *p, object_t *parent) {
    return create_user_defined_object(p, (object_array_t){&parent, 1});
}

static object_t *function(process_t *p, object_t *closure) {
    return create_function_object(p, NULL, 0, 0, closure, NULL, true);
}

static field_cell_t *field(object_t *owner, object_t *fn, access_modifier_t modifier) {
    field_cell_t *cell = create_object_field(owner,
                                             init_key(),
                                             fn,
                                             false,
                                             modifier,
                                             get_object_module(owner),
                                             get_object_access_domain(owner),
                                             true);
    assert(cell);
    return cell;
}

static init_snapshot_t *expect(process_t *p, object_t *receiver, object_array_t expected) {
    init_snapshot_result_t r = collect_init_snapshot(p, receiver);
    assert(r.snapshot && !r.exception.value);
    assert(get_init_snapshot_receiver(r.snapshot) == receiver);
    object_array_t actual = get_init_snapshot_functions(r.snapshot);
    assert(actual.size == expected.size);
    for (size_t i = 0; i < actual.size; i++)
        assert(actual.items[i] == expected.items[i]);
    return r.snapshot;
}

static void order_case(void) {
    process_t *p = create_process();
    object_t *left = object(p, get_root_object());
    object_t *right = object(p, get_root_object());
    field(left, get_function_sign(), ACCESS_EXTERNAL);
    field(right, get_function_atan(), ACCESS_EXTERNAL);
    object_t *independent[] = {left, right};
    object_t *combined = create_user_defined_object(p, (object_array_t){independent, 2});
    object_t *reverse[] = {get_function_atan(), get_function_sign()};
    init_snapshot_t *pair = expect(p, combined, (object_array_t){reverse, 2});
    destroy_init_snapshot(pair);
    DECREF(combined);
    DECREF(left);
    DECREF(right);
    object_t *base = object(p, get_root_object());
    object_t *a = object(p, base), *b = object(p, base);
    object_t *fbase = function(p, base), *fa = function(p, a), *fb = function(p, b);
    field(base, fbase, ACCESS_PRIVATE);
    field(a, fa, ACCESS_PROTECTED);
    field(b, fb, ACCESS_FILE);
    object_t *parents[] = {a, b, a};
    object_t *receiver = create_user_defined_object(p, (object_array_t){parents, 3});
    /* Repeated [A,B,A] visits the rightmost A first. Shared base is visited once. */
    object_t *wanted[] = {fbase, fa, fb};
    init_snapshot_t *s = expect(p, receiver, (object_array_t){wanted, 3});
    destroy_init_snapshot(s);
    DECREF(receiver);
    receiver = create_user_defined_object(p, (object_array_t){parents, 2});
    object_t *diamond[] = {fbase, fb, fa};
    s = expect(p, receiver, (object_array_t){diamond, 3});
    /* For pure inheritance, this must be exactly reverse ordinary lookup topology. */
    object_array_t topology = get_object_topology(receiver);
    size_t n = 0;
    for (size_t i = topology.size; i > 0; i--) {
        field_cell_t *cell = get_own_field_cell(topology.items[i - 1], init_key());
        if (cell)
            assert(diamond[n++] == cell->object);
    }
    assert(n == 3);
    destroy_init_snapshot(s);
    /* Same function on distinct objects is not deduplicated by function identity. */
    assert(set_field_cell_value(b, get_own_field_cell(b, init_key()), fa) == MSTAT_OK);
    object_t *shared[] = {fbase, fa, fa};
    s = expect(p, receiver, (object_array_t){shared, 3});
    destroy_init_snapshot(s);
    DECREF(receiver);
    DECREF(fbase);
    DECREF(fa);
    DECREF(fb);
    DECREF(a);
    DECREF(b);
    DECREF(base);
    destroy_process(p);
}

static void lexical_case(void) {
    process_t *p = create_process();
    object_t *lexical = object(p, get_root_object());
    field(lexical, get_static_integer_object(99), ACCESS_PRIVATE);
    object_t *base = object(p, lexical);
    set_inheritance_prototype_count(base, 0);
    object_t *receiver = object(p, base);
    /* Own receiver init belongs to future descendants, and must not be validated here. */
    field(receiver, get_null_object(), ACCESS_EXTERNAL);
    init_snapshot_t *s = expect(p, receiver, (object_array_t){NULL, 0});
    destroy_init_snapshot(s);
    field_cell_t *own = field(base, get_function_sign(), ACCESS_PRIVATE);
    object_t *expected[] = {get_function_sign()};
    for (int mode = ACCESS_FILE; mode <= ACCESS_PRIVATE; mode++) {
        own->modifier = (access_modifier_t)mode; /* Fixture metadata only. */
        s = expect(p, receiver, (object_array_t){expected, 1});
        destroy_init_snapshot(s);
    }
    property_lookup_t lookup =
        lookup_accessible_property(&p->main_thread->context->access, base, init_key(), true);
    assert(lookup.status == MSTAT_ACCESS_DENIED);
    /* An ancestor that merely inherits init must not add a second copy. */
    object_t *middle = object(p, base);
    object_t *leaf = object(p, middle);
    s = expect(p, leaf, (object_array_t){expected, 1});
    destroy_init_snapshot(s);
    DECREF(leaf);
    DECREF(middle);
    DECREF(receiver);
    DECREF(base);
    DECREF(lexical);
    destroy_process(p);
}

static unsigned calls;

static operation_result_t count_call(object_t **args, uint16_t count, thread_t *thread) {
    (void)args;
    (void)count;
    (void)thread;
    calls++;
    return operation_success(get_null_object());
}

static void validation_case(void) {
    process_t *p = create_process();
    static builtin_function_object_t storage;
    static const builtin_function_t descriptor = {.name = U"snapshot_probe", .execute = count_call};
    object_t *fn = get_builtin_function_object(&storage, &descriptor);
    object_t *a = object(p, get_root_object()), *b = object(p, get_root_object());
    field_cell_t *bad = field(a, get_static_integer_object(42), ACCESS_PRIVATE);
    field(b, fn, ACCESS_EXTERNAL);
    object_t *parents[] = {a, b};
    object_t *receiver = create_user_defined_object(p, (object_array_t){parents, 2});
    object_t *callable = object(p, get_root_object());
    static object_static_string_t call_key;
    assert(create_object_property(callable, get_static_string_object(&call_key, U"()"), fn, true)
           == MSTAT_OK);
    object_t *invalid[] = {get_static_integer_object(42), get_null_object(), callable};
    size_t roots = p->gc_roots_size;
    for (size_t i = 0; i < 3; i++) {
        assert(set_field_cell_value(a, bad, invalid[i]) == MSTAT_OK);
        init_snapshot_result_t r = collect_init_snapshot(p, receiver);
        assert(!r.snapshot && r.exception.value == get_exception_invalid_operation());
        assert(p->gc_roots_size == roots && calls == 0);
        clear_exception(&r.exception);
    }
    assert(set_field_cell_value(a, bad, fn) == MSTAT_OK);
    object_t *expected[] = {fn, fn};
    init_snapshot_t *s = expect(p, receiver, (object_array_t){expected, 2});
    assert(calls == 0);
    destroy_init_snapshot(s);
    destroy_init_snapshot(NULL);
    DECREF(callable);
    DECREF(receiver);
    DECREF(a);
    DECREF(b);
    destroy_process(p);
}

static bool live(process_t *p, object_t *o) {
    for (object_t *item = p->objects.head; item; item = item->next)
        if (item == o)
            return true;
    return false;
}

static void ownership_case(void) {
    process_t *p = create_process();
    object_t *base = object(p, get_root_object());
    object_t *closure = object(p, get_root_object());
    object_t *fn = function(p, closure);
    field_cell_t *cell = field(base, fn, ACCESS_EXTERNAL);
    object_t *receiver = object(p, base);
    object_t *expected[] = {fn};
    size_t roots = p->gc_roots_size;
    init_snapshot_t *first = expect(p, receiver, (object_array_t){expected, 1});
    assert(set_field_cell_value(base, cell, get_function_sign()) == MSTAT_OK);
    object_t *replacement[] = {get_function_sign()};
    init_snapshot_t *second = expect(p, receiver, (object_array_t){replacement, 1});
    assert(p->gc_roots_size == roots + 2);
    DECREF(fn);
    DECREF(closure);
    DECREF(base);
    DECREF(receiver);
    collect_garbage(p);
    assert(live(p, receiver) && live(p, fn) && live(p, closure));
    assert(get_init_snapshot_functions(first).items[0] == fn);
    assert(get_function_access(fn));
    destroy_init_snapshot(first);
    assert(p->gc_roots_size == roots + 1);
    collect_garbage(p);
    assert(live(p, receiver) && !live(p, fn) && !live(p, closure));
    destroy_init_snapshot(second);
    assert(p->gc_roots_size == roots);
    collect_garbage(p);
    assert(!live(p, receiver) && !live(p, base));
    destroy_process(p);
}

static void depth_case(void) {
    process_t *p = create_process();
    object_t *node = object(p, get_root_object());
    field(node, get_function_sign(), ACCESS_EXTERNAL);
    for (size_t i = 0; i < 512; i++) {
        object_t *next = object(p, node);
        DECREF(node);
        node = next;
    }
    object_t *expected[] = {get_function_sign()};
    init_snapshot_t *s = expect(p, node, (object_array_t){expected, 1});
    destroy_init_snapshot(s);
    DECREF(node);
    destroy_process(p);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    void (*run)(void) = NULL;
    if (!strcmp(argv[1], "order"))
        run = order_case;
    else if (!strcmp(argv[1], "lexical"))
        run = lexical_case;
    else if (!strcmp(argv[1], "validation"))
        run = validation_case;
    else if (!strcmp(argv[1], "ownership"))
        run = ownership_case;
    else if (!strcmp(argv[1], "depth"))
        run = depth_case;
    else
        return 2;
    run(); /* Warm up immortal built-in state before comparing allocator accounting. */
    size_t before = get_allocated_memory_size();
    run();
    assert(get_allocated_memory_size() == before);
    printf("init snapshot %s passed\n", argv[1]);
    return 0;
}
