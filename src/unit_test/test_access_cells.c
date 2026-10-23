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

bool test_access_observation(void) {
    process_t *proc = create_process();
    module_t *foreign = create_module_identity();
    object_t *root = get_root_object();
    object_t *obj = create_user_defined_object_in_module(proc, (object_array_t){&root, 1}, foreign);
    access_domain_t *domain = get_object_access_domain(obj);
    const goat_char_t *names[] = {U"module", U"public", U"protected", U"private"};
    const access_modifier_t modifiers[] = {ACCESS_MODULE,
                                           ACCESS_PUBLIC,
                                           ACCESS_PROTECTED,
                                           ACCESS_PRIVATE};
    for (size_t i = 0; i < 4; i++) {
        object_t *key =
            create_string_object(proc, (string_value_t){names[i], goat_strlen(names[i]), false});
        ASSERT(create_object_field(obj,
                                   key,
                                   get_boolean_object(true),
                                   false,
                                   modifiers[i],
                                   foreign,
                                   domain,
                                   true));
        DECREF(key);
    }
    function_access_t observers[3];
    init_function_access(&observers[0], proc->module);
    init_function_access(&observers[1], foreign);
    init_function_access(&observers[2], foreign);
    add_function_access_domain(&observers[2], domain);
    /* Even a privileged observer must not enumerate internal same-key cells. */
    access_domain_t *hidden_domain = create_access_domain(foreign);
    object_t *hidden_key = create_string_object(proc, STATIC_STRING(U"public"));
    ASSERT(create_object_field(obj,
                               hidden_key,
                               get_boolean_object(false),
                               false,
                               ACCESS_PRIVATE,
                               foreign,
                               hidden_domain,
                               false));
    add_function_access_domain(&observers[2], hidden_domain);
    DECREF(hidden_key);
    release_access_domain(hidden_domain);
    const goat_char_t *expected[] = {
        U"[{\"public\":true}]",
        U"[{\"module\":true,\"public\":true}]",
        U"[{\"module\":true,\"private\":true,\"protected\":true,\"public\":true}]"};
    size_t counts[] = {1, 2, 4};
    object_t *array = create_array_object(proc, (object_array_t){&obj, 1});
    for (size_t pass = 0; pass < 3; pass++) {
        for (size_t i = 0; i < 3; i++) {
            object_t *keys = snapshot_object_keys_with_access(proc, obj, &observers[i]);
            ASSERT(get_array_elements(keys).size == counts[i]);
            DECREF(keys);
            string_value_t text = object_to_string_with_access(array, &observers[i]);
            ASSERT(!goat_strcmp(text.data, expected[i]));
            FREE_STRING(text);
        }
    }
    /* A foreign caller copies all fields, but still sees only public declarations. */
    object_t *copy = flat_object_with_access(proc, obj, &observers[0]);
    ASSERT(get_object_field_count(copy) == 5);
    object_t *keys = snapshot_object_keys_with_access(proc, copy, &observers[0]);
    ASSERT(get_array_elements(keys).size == 1);
    DECREF(keys);
    DECREF(copy);
    for (size_t i = 0; i < 3; i++)
        clear_function_access(&observers[i], true);
    DECREF(array);
    DECREF(obj);
    release_module_identity(foreign);
    destroy_process(proc);
    return true;
}

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
    object_t *key = get_static_string_object(&name, U"clone");
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
    object_t *key = create_string_object(proc, STATIC_STRING(U"x"));
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
    object_t *key = create_string_object(proc, STATIC_STRING(U"hidden"));
    object_t *value = create_string_object(proc, STATIC_STRING(U"retained"));
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
    ASSERT(!goat_strcmp(text.data, U"retained"));
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
        object_t *key = create_string_object(proc, STATIC_STRING(U"cycle"));
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

/** @brief Cross-module provenance, cell/function identity maps and GC of repeated copies. */
bool test_access_clone_methods(void) {
    process_t *proc = create_process();
    size_t baseline = proc->objects.size;
    module_t *foreign = create_module_identity();
    object_static_string_t x_name = {0}, y_name = {0}, f_name = {0}, g_name = {0}, extra_name = {0};
    object_t *x = get_static_string_object(&x_name, U"x");
    object_t *y = get_static_string_object(&y_name, U"y");
    object_t *f = get_static_string_object(&f_name, U"f");
    object_t *g = get_static_string_object(&g_name, U"g");
    object_t *extra = get_static_string_object(&extra_name, U"extra");
    function_access_t caller;
    init_function_access(&caller, proc->module);
    for (int iteration = 0; iteration < 30; iteration++) {
        object_t *root = get_root_object();
        object_t *original =
            create_user_defined_object_in_module(proc, (object_array_t){&root, 1}, foreign);
        field_cell_t *old_cell = create_object_field(original,
                                                     x,
                                                     get_boolean_object(false),
                                                     false,
                                                     ACCESS_PRIVATE,
                                                     foreign,
                                                     get_object_access_domain(original),
                                                     true);
        old_cell->binding_unit = retain_module_identity(foreign);
        old_cell->declaration_id = 1;
        ASSERT(create_object_field(original,
                                   y,
                                   get_null_object(),
                                   false,
                                   ACCESS_MODULE,
                                   foreign,
                                   get_object_access_domain(original),
                                   true));
        object_t *method = create_function_object(proc, NULL, 0, 0, original, NULL, true);
        ASSERT(create_object_field(original,
                                   f,
                                   method,
                                   true,
                                   ACCESS_PUBLIC,
                                   foreign,
                                   get_object_access_domain(original),
                                   true));
        ASSERT(create_object_field(original,
                                   g,
                                   method,
                                   true,
                                   ACCESS_PUBLIC,
                                   foreign,
                                   get_object_access_domain(original),
                                   true));
        object_t *copy = clone_object_with_access(proc, original, &caller);
        object_t *second = clone_object_with_access(proc, original, &caller);
        field_cell_t *cell = get_own_field_cell(copy, x);
        object_t *adapted = get_own_field_cell(copy, f)->object;
        function_access_t *rights = get_function_access(adapted);
        ASSERT(cell != old_cell && cell->domain != old_cell->domain);
        ASSERT(cell->module == foreign && rights->module == foreign);
        ASSERT(get_object_module(copy) == proc->module);
        ASSERT(adapted != method && adapted == get_own_field_cell(copy, g)->object);
        ASSERT(adapted != get_own_field_cell(second, f)->object);
        ASSERT(!can_access_field(get_function_access(method), cell));
        ASSERT(can_access_field(rights, cell) && can_access_field(rights, old_cell));
        ASSERT(!can_access_field(rights, get_own_field_cell(second, x)));
        ASSERT(!can_access_field(&caller, cell));
        ASSERT(!can_access_field(&caller, get_own_field_cell(copy, y)));
        ASSERT(can_access_field(rights, get_own_field_cell(copy, y)));
        ASSERT(create_accessible_property(&caller, original, extra, root, false)
               == MSTAT_FOREIGN_OBJECT);
        ASSERT(create_accessible_property(&caller, copy, extra, root, false) == MSTAT_OK);
        ASSERT(lookup_bound_property(rights, copy, foreign, 1).cell == cell);
        ASSERT(rights->captures->size == 1);
        field_capture_t *capture = rights->captures->data[0];
        ASSERT(capture->storage == copy && capture->cell == cell);
        ASSERT(set_field_cell_value(copy, cell, get_boolean_object(true)) == MSTAT_OK);
        ASSERT(old_cell->object == get_boolean_object(false));
        ASSERT(set_field_cell_value(copy, get_own_field_cell(copy, f), method)
               == MSTAT_PROPERTY_IS_CONSTANT);
        add_gc_root(proc, adapted);
        DECREF(copy);
        DECREF(second);
        DECREF(method);
        DECREF(original);
        collect_garbage(proc);
        ASSERT(capture->cell->object == get_boolean_object(true));
        ASSERT(get_own_field_cell(capture->storage, f)->object == adapted);
        /* A detached method alone keeps its copied cell and alias alive. */
        remove_gc_root(proc, adapted);
        collect_garbage(proc);
        ASSERT(proc->objects.size == baseline);
    }
    clear_function_access(&caller, true);
    release_module_identity(foreign);
    destroy_process(proc);
    return true;
}

/** @brief Same-key, same-bytecode-ID cells stay distinct after merging prototype storage. */
bool test_access_flat_cells(void) {
    process_t *proc = create_process();
    module_t *foreign = create_module_identity();
    function_access_t caller;
    init_function_access(&caller, proc->module);
    object_static_string_t x_name = {0}, f_name = {0}, l_name = {0}, r_name = {0};
    object_t *x = get_static_string_object(&x_name, U"x");
    object_t *f = get_static_string_object(&f_name, U"f");
    object_t *l = get_static_string_object(&l_name, U"left");
    object_t *r = get_static_string_object(&r_name, U"right");
    size_t baseline = proc->objects.size;
    for (int iteration = 0; iteration < 20; iteration++) {
        object_t *root = get_root_object();
        object_t *parents[2], *methods[2];
        for (size_t i = 0; i < 2; i++) {
            parents[i] =
                create_user_defined_object_in_module(proc, (object_array_t){&root, 1}, foreign);
            /* Exercise GC of the internal lexical view as well as its merged storage. */
            set_inheritance_prototype_count(parents[i], 0);
            field_cell_t *cell = create_object_field(parents[i],
                                                     x,
                                                     get_boolean_object(i != 0),
                                                     true,
                                                     ACCESS_PRIVATE,
                                                     foreign,
                                                     get_object_access_domain(parents[i]),
                                                     true);
            cell->declaration_id = 1;
            cell->binding_unit = retain_module_identity(foreign);
            methods[i] = create_function_object(proc, NULL, 0, 0, parents[i], NULL, true);
            ASSERT(create_object_field(parents[i],
                                       f,
                                       methods[i],
                                       true,
                                       ACCESS_PUBLIC,
                                       foreign,
                                       get_object_access_domain(parents[i]),
                                       true));
        }
        object_t *source = create_user_defined_object(proc, (object_array_t){parents, 2});
        ASSERT(create_object_property(source, l, methods[0], true) == MSTAT_OK);
        ASSERT(create_object_property(source, r, methods[1], true) == MSTAT_OK);
        object_t *copy = flat_object_with_access(proc, source, &caller);
        ASSERT(get_object_module(copy) == caller.module);
        ASSERT(get_object_prototypes(copy).size == 1
               && get_object_prototypes(copy).items[0] == root);
        ASSERT(get_object_field_count(copy) == 6 && get_object_keys(copy).size == 4);
        object_t *left = get_object_property(copy, l), *right = get_object_property(copy, r);
        ASSERT(left != right && left != methods[0] && right != methods[1]);
        function_access_t *la = get_function_access(left), *ra = get_function_access(right);
        ASSERT(la->module == foreign && ra->module == foreign);
        property_lookup_t lc = lookup_bound_property(la, root, foreign, 1);
        property_lookup_t rc = lookup_bound_property(ra, root, foreign, 1);
        ASSERT(lc.status == MSTAT_OK && rc.status == MSTAT_OK && lc.cell != rc.cell);
        ASSERT(lc.storage == copy && rc.storage == copy);
        ASSERT(lc.value == get_boolean_object(false) && rc.value == get_boolean_object(true));
        ASSERT(lc.cell->domain != rc.cell->domain && lc.cell->module == foreign);
        ASSERT(!lc.cell->is_constant && !rc.cell->is_constant);
        ASSERT(!can_access_field(&caller, lc.cell) && !can_access_field(la, rc.cell));
        ASSERT(get_object_property(copy, x)
               == lookup_accessible_property(&caller, source, x, false).cell->object);
        ASSERT(set_field_cell_value(copy, lc.cell, get_boolean_object(true)) == MSTAT_OK);
        ASSERT(get_object_property(parents[0], x) == get_boolean_object(false));
        add_gc_root(proc, left);
        DECREF(copy);
        DECREF(source);
        for (size_t i = 0; i < 2; i++) {
            DECREF(methods[i]);
            DECREF(parents[i]);
        }
        collect_garbage(proc);
        ASSERT(lc.cell->object == get_boolean_object(true));
        ASSERT(get_object_field_count(lc.storage) == 6);
        remove_gc_root(proc, left);
        collect_garbage(proc);
        ASSERT(proc->objects.size == baseline);
    }
    clear_function_access(&caller, true);
    release_module_identity(foreign);
    destroy_process(proc);
    return true;
}

bool test_access_clone_metadata(void) {
    process_t *proc = create_process();
    object_t *root = get_root_object();
    module_t *module = create_module_identity();
    object_t *obj = create_user_defined_object_in_module(proc, (object_array_t){&root, 1}, module);
    access_domain_t *extra = create_access_domain(module);
    object_t *key = create_string_object(proc, STATIC_STRING(U"same"));
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
