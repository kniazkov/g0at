/** @file test_access_cells.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Tests the internal stage-A model without enabling access-modifier syntax.
 */
#include "test_access_cells.h"

#include "lib/allocate.h"
#include "model/access.h"
#include "model/context.h"
#include "model/object_stack.h"
#include "model/process.h"
#include "model/thread.h"
#include "test_macro.h"
#include "vm/gc.h"

#include <stdio.h>

static object_t *new_object(process_t *proc) {
    object_t *root = get_root_object();
    return create_user_defined_object(proc, (object_array_t){&root, 1});
}

bool test_access_identities(void) {
    process_t *proc = create_process();
    process_t *other = create_process();
    ASSERT(proc->module != other->module);
    object_t *a = new_object(proc);
    object_t *b = new_object(proc);
    ASSERT(get_object_module(a) == proc->module);
    ASSERT(get_object_access_domain(a) != get_object_access_domain(b));
    access_domain_t *old = retain_access_domain(get_object_access_domain(a));
    DECREF(a);
    a = new_object(proc); /* Object pool reuse must not reuse a surviving domain identity. */
    ASSERT(get_object_access_domain(a) != old);
    ASSERT(get_access_domain_module(old) == proc->module);
    release_access_domain(old);
    object_static_string_t name = {0};
    object_t *key = get_static_string_object(&name, L"clone");
    ASSERT(get_object_property(get_root_object(), key));
    ASSERT(get_object_property_modifier(get_root_object(), key) == ACCESS_PUBLIC);
    ASSERT(get_object_module(get_root_object()) == get_builtin_module_identity());
    DECREF(a);
    DECREF(b);
    destroy_process(other);
    destroy_process(proc);
    return true;
}

bool test_access_cells(void) {
    process_t *proc = create_process();
    object_t *obj = new_object(proc);
    object_t *key = create_string_object(proc, STATIC_STRING(L"x"));
    object_t *first = create_integer_object(proc, 10);
    object_t *second = create_integer_object(proc, 20);
    module_t *foreign = create_module_identity();
    access_domain_t *domain = create_access_domain(foreign);
    field_cell_t *a = create_object_field(obj,
                                          key,
                                          first,
                                          false,
                                          ACCESS_PRIVATE,
                                          proc->module,
                                          get_object_access_domain(obj),
                                          true);
    field_cell_t *b =
        create_object_field(obj, key, second, true, ACCESS_PROTECTED, foreign, domain, false);
    ASSERT(a && b && a != b);
    ASSERT(get_object_field_count(obj) == 2);
    ASSERT(get_object_keys(obj).size == 1);
    ASSERT(get_own_field_cell(obj, key) == a);
    ASSERT(get_domain_field_cell(obj, key, domain) == b);
    ASSERT(get_object_property(obj, key) == first);
    ASSERT(get_object_property_modifier(obj, key) == ACCESS_PRIVATE);
    ASSERT(b->module == foreign && get_object_module(obj) == proc->module);
    ASSERT(!create_object_field(obj, key, first, false, ACCESS_PUBLIC, foreign, domain, false));
    ASSERT(set_field_cell_value(obj, b, first) == MSTAT_PROPERTY_IS_CONSTANT);
    ASSERT(set_field_cell_value(obj, a, second) == MSTAT_OK);
    ASSERT(a->object == second && b->object == second);
    /* Arbitrary object keys and stable addresses while the inventory grows. */
    for (int i = 0; i < 100; i++) {
        object_t *k = new_object(proc);
        ASSERT(create_object_field(obj,
                                   k,
                                   get_null_object(),
                                   false,
                                   ACCESS_MODULE,
                                   proc->module,
                                   get_object_access_domain(obj),
                                   true));
        DECREF(k);
    }
    ASSERT(get_object_field_at(obj, 0) == a && get_object_field_at(obj, 1) == b);
    ASSERT(!get_object_field_at(obj, 102));
    DECREF(first);
    DECREF(second);
    DECREF(key);
    release_module_identity(foreign);
    release_access_domain(domain);
    DECREF(obj);
    destroy_process(proc);
    return true;
}

bool test_access_captures(void) {
    process_t *proc = create_process();
    object_t *storage = new_object(proc);
    object_t *key = create_string_object(proc, STATIC_STRING(L"hidden"));
    object_t *value = create_string_object(proc, STATIC_STRING(L"retained"));
    field_cell_t *cell = create_object_field(storage,
                                             key,
                                             value,
                                             false,
                                             ACCESS_PRIVATE,
                                             proc->module,
                                             get_object_access_domain(storage),
                                             false);
    ASSERT(cell);
    object_t *f = create_function_object(proc, NULL, 0, 0, get_root_object(), NULL, true);
    object_t *g = create_function_object(proc, NULL, 0, 0, get_root_object(), NULL, true);
    function_access_t *fa = get_function_access(f), *ga = get_function_access(g);
    ASSERT(add_function_field_capture(fa, storage, cell));
    ASSERT(add_function_field_capture(fa, storage, cell));
    ASSERT(add_function_field_capture(ga, storage, cell));
    ASSERT(fa->captures->size == 1);
    ASSERT(!add_function_field_capture(ga, get_root_object(), cell));
    add_function_access_domain(fa, cell->domain);
    add_function_access_domain(fa, cell->domain);
    ASSERT(fa->domains->size == 1);
    add_gc_root(proc, f);
    add_gc_root(proc, g);
    DECREF(storage);
    DECREF(key);
    DECREF(value);
    collect_garbage(proc);
    field_capture_t *fc = fa->captures->data[0], *gc = ga->captures->data[0];
    ASSERT(fc->cell == gc->cell && fc->storage == gc->storage);
    string_value_t text = convert_object_to_string(fc->cell->object);
    ASSERT(!wcscmp(text.data, L"retained"));
    FREE_STRING(text);
    /* An identical borrowed value must be retained before its old reference is dropped. */
    ASSERT(set_field_cell_value(fc->storage, fc->cell, fc->cell->object) == MSTAT_OK);
    ASSERT(set_field_cell_value(fc->storage, fc->cell, get_boolean_object(true)) == MSTAT_OK);
    ASSERT(gc->cell->object == get_boolean_object(true));
    remove_gc_root(proc, f);
    DECREF(f);
    collect_garbage(proc);
    ASSERT(gc->cell->object == get_boolean_object(true));
    remove_gc_root(proc, g);
    DECREF(g);
    collect_garbage(proc);
    destroy_process(proc);
    return true;
}

bool test_access_cycles(void) {
    process_t *proc = create_process();
    size_t baseline = proc->objects.size;
    for (int i = 0; i < 40; i++) {
        object_t *storage = new_object(proc);
        object_t *f = create_function_object(proc, NULL, 0, 0, storage, NULL, true);
        object_t *key = create_string_object(proc, STATIC_STRING(L"cycle"));
        field_cell_t *cell = create_object_field(storage,
                                                 key,
                                                 f,
                                                 false,
                                                 ACCESS_PRIVATE,
                                                 proc->module,
                                                 get_object_access_domain(storage),
                                                 false);
        ASSERT(add_function_field_capture(get_function_access(f), storage, cell));
        DECREF(storage);
        DECREF(f);
        DECREF(key);
        collect_garbage(proc);
        ASSERT(proc->objects.size == baseline);
    }
    destroy_process(proc);
    return true;
}

bool test_access_contexts(void) {
    process_t *proc = create_process();
    context_t *caller = proc->main_thread->context;
    module_t *module = create_module_identity();
    object_t *root = get_root_object();
    context_t *callee =
        create_context_with_protos_in_module(proc, caller, (object_array_t){&root, 1}, module);
    ASSERT(callee->module == module && caller->module == proc->module);
    ASSERT(get_object_module(callee->data) == module);
    object_t *f = create_function_object(proc, NULL, 0, 0, callee->data, NULL, true);
    ASSERT(get_function_access(f)->module == module);
    ASSERT(get_function_access(f)->domains->data[0] == get_object_access_domain(callee->data));
    ASSERT(call_object(f, 0, NULL, proc->main_thread));
    ASSERT(proc->main_thread->context->module == module);
    ASSERT(proc->main_thread->context->previous == caller);
    access_domain_t *activation =
        retain_access_domain(get_object_access_domain(proc->main_thread->context->data));
    proc->main_thread->context = destroy_context(proc->main_thread->context);
    object_t *result = pop_object_from_stack(proc->main_thread->data_stack);
    DECREF(result);
    ASSERT(call_object(f, 0, NULL, proc->main_thread));
    ASSERT(get_object_access_domain(proc->main_thread->context->data) != activation);
    release_access_domain(activation);
    proc->main_thread->context = destroy_context(proc->main_thread->context);
    result = pop_object_from_stack(proc->main_thread->data_stack);
    DECREF(result);
    ASSERT(proc->main_thread->context->module == proc->module);
    context_t *nested = create_context(proc, callee, NULL);
    ASSERT(nested->module == module);
    /* The callee's prototypes do not retain the caller: GC must mark all active contexts. */
    proc->main_thread->context = nested;
    collect_garbage(proc);
    ASSERT(get_object_module(caller->data) == proc->module);
    proc->main_thread->context = destroy_context(nested);
    proc->main_thread->context = destroy_context(callee);
    release_module_identity(module);
    /* f was not a registered root and was swept; do not use its pointer here. */
    destroy_process(proc);
    return true;
}

bool test_access_clone_metadata(void) {
    process_t *proc = create_process();
    object_t *root = get_root_object();
    module_t *module = create_module_identity();
    object_t *obj = create_user_defined_object_in_module(proc, (object_array_t){&root, 1}, module);
    access_domain_t *extra = create_access_domain(module);
    object_t *key = create_string_object(proc, STATIC_STRING(L"same"));
    field_cell_t *visible = create_object_field(obj,
                                                key,
                                                obj,
                                                true,
                                                ACCESS_PUBLIC,
                                                module,
                                                get_object_access_domain(obj),
                                                true);
    ASSERT(visible);
    ASSERT(create_object_field(obj,
                               key,
                               get_boolean_object(true),
                               false,
                               ACCESS_PRIVATE,
                               module,
                               extra,
                               false));
    object_t *copy = clone_object(proc, obj);
    ASSERT(get_object_module(copy) == proc->module);
    ASSERT(get_object_field_count(copy) == 2 && get_object_keys(copy).size == 1);
    field_cell_t *a = get_own_field_cell(copy, key), *b = get_domain_field_cell(copy, key, extra);
    ASSERT(a != visible && a->object == copy && a->is_constant && a->module == module);
    ASSERT(b && b->modifier == ACCESS_PRIVATE);
    ASSERT(get_object_access_domain(copy) != get_object_access_domain(obj));
    DECREF(obj);
    release_access_domain(extra);
    release_module_identity(module);
    DECREF(key);
    add_gc_root(proc, copy);
    collect_garbage(proc);
    ASSERT(b->object == get_boolean_object(true));
    remove_gc_root(proc, copy);
    DECREF(copy);
    destroy_process(proc);
    return true;
}
