/** @file test_gc_ownership.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Reject cross-heap edges before publishing or retaining any batch member.
 */
#include "lib/memory/allocate.h"
#include "model/array.h"
#include "model/array_internal.h"
#include "model/context.h"
#include "model/thread.h"
#include "test_macro.h"
#include "test_model.h"
#include "vm/vm.h"

bool test_gc_process_ownership(void) {
    size_t initial = get_allocated_memory_size();
    process_t *a = create_process(), *b = create_process();
    object_t *own = create_array_object(a, (object_array_t){0});
    object_t *foreign = create_array_object(b, (object_array_t){0});
    object_t *batch[] = {own, get_null_object(), foreign};
    size_t objects = a->objects.size, bytes = get_allocated_memory_size();
    ASSERT(!create_array_object(a, (object_array_t){batch, 3}));
    ASSERT(!create_read_only_array_object(a, (object_array_t){batch, 3}));
    ASSERT(!create_user_defined_object(a, (object_array_t){batch, 3}));
    ASSERT(!create_function_object(a, NULL, 0, 0, foreign, NULL, false));
    ASSERT(!create_function_object(a, batch, 3, 0, a->main_thread->context->data, NULL, false));
    ASSERT(!create_context(a, b->main_thread->context, NULL));
    ASSERT(!create_thread(a, b->main_thread->context));
    ASSERT(!capture_context_arguments(a->main_thread->context, (object_array_t){batch, 3}));
    ASSERT(!a->main_thread->context->arguments);
    ASSERT(objects == a->objects.size && bytes == get_allocated_memory_size());
    ASSERT(((array_object_t *)own)->refs == 1 && ((array_object_t *)foreign)->refs == 1);

    object_t *container = create_array_object(a, (object_array_t){batch, 2});
    size_t capacity = ((array_object_t *)container)->capacity;
    bytes = get_allocated_memory_size();
    operation_result_t result = insert_array_elements(container, 1, batch, 3);
    ASSERT(result.is_exception);
    DECREF(result.value);
    ASSERT(get_array_elements(container).size == 2);
    ASSERT(((array_object_t *)container)->capacity == capacity);
    object_t *zero = get_static_integer_object(0);
    result = container->vtbl->set_index(a, container, (object_array_t){&zero, 1}, foreign);
    ASSERT(result.is_exception);
    DECREF(result.value);
    ASSERT(get_array_elements(container).items[0] == own);
    ASSERT(((array_object_t *)own)->refs == 2 && ((array_object_t *)foreign)->refs == 1);
    ASSERT(bytes == get_allocated_memory_size());
    ASSERT(!clone_object(a, foreign));

    object_t *root = get_root_object();
    object_t *dict = create_user_defined_object(a, (object_array_t){&root, 1});
    object_t *other = create_user_defined_object(b, (object_array_t){&root, 1});
    ASSERT(create_object_property(dict, zero, own, false) == MSTAT_OK);
    ASSERT(create_object_property(dict, foreign, own, false) == MSTAT_FOREIGN_OBJECT);
    ASSERT(create_object_property(dict, get_null_object(), foreign, false) == MSTAT_FOREIGN_OBJECT);
    ASSERT(set_object_property(dict, zero, foreign) == MSTAT_FOREIGN_OBJECT);
    ASSERT(get_object_property(dict, zero) == own && get_object_field_count(dict) == 1);
    field_cell_t *cell = get_own_field_cell(dict, zero);
    cell->pending_initialization = true;
    ASSERT(initialize_field_cell(dict, cell, foreign) == MSTAT_FOREIGN_OBJECT);
    ASSERT(cell->pending_initialization && cell->object == own);
    cell->pending_initialization = false;
    ASSERT(!create_object_field(dict,
                                get_null_object(),
                                foreign,
                                false,
                                ACCESS_FILE,
                                get_object_module(dict),
                                get_object_access_domain(dict),
                                true));
    ASSERT(!clone_object(a, other));
    ASSERT(!flat_object_with_access(a, other, &a->main_thread->context->access));

    function_access_t left, right;
    init_function_access(&left, a->module);
    init_function_access(&right, a->module); /* Module identity is not heap ownership. */
    ASSERT(add_lexical_scope(&left, dict));
    ASSERT(add_lexical_scope(&left, root));
    ASSERT(add_lexical_scope(&right, other));
    ASSERT(create_object_property(other, zero, foreign, false) == MSTAT_OK);
    ASSERT(!add_lexical_scope(&left, other));
    ASSERT(!add_function_field_capture(&left, other, get_own_field_cell(other, zero)));
    ASSERT(!inherit_function_access(&left, &right));
    ASSERT(left.lexical_scopes->size == 2 && left.captures->size == 0);
    ASSERT(add_function_field_capture(&left, dict, cell));
    clear_function_access(&left, true);
    clear_function_access(&right, true);

    object_stack_t *stack = a->main_thread->data_stack;
    ASSERT(push_object_onto_stack(stack, foreign) == BAD_STACK_INDEX && stack->size == 0);
    INCREF(own);
    ASSERT(push_object_onto_stack(stack, own) == 0);
    ASSERT(!replace_object_on_stack(stack, foreign, 0));
    ASSERT(stack->size == 1 && peek_object_from_stack(stack, 0) == own);
    ASSERT(replace_object_on_stack(stack, get_null_object(), 0));
    object_t *popped = pop_object_from_stack(stack);
    DECREF(popped);
    object_t *child = create_array_object(a, (object_array_t){0});
    object_t *parent = create_array_object(a, (object_array_t){&child, 1});
    DECREF(child); /* Only the stack container now owns the borrowed replacement. */
    ASSERT(push_object_onto_stack(stack, parent) == 0);
    ASSERT(replace_object_on_stack(stack, child, 0));
    ASSERT(((array_object_t *)child)->refs == 1);
    popped = pop_object_from_stack(stack);
    DECREF(popped);

    /* Validation must precede execution-state dereference, declarations and worker publication. */
    a->execution = (struct vm_execution_t *)a;
    result = spawn_program_thread(a->main_thread, get_function_print(), batch, 3);
    ASSERT(result.is_exception && a->active_threads == 0 && a->main_thread->next == a->main_thread);
    DECREF(result.value);
    object_t *foreign_func = create_function_object(b, NULL, 0, 0, other, NULL, false);
    result = spawn_program_thread(a->main_thread, foreign_func, NULL, 0);
    ASSERT(result.is_exception && stack->size == 0);
    DECREF(result.value);
    a->execution = NULL;
    ASSERT(!clone_object(a, foreign_func));
    ASSERT(!call_object(foreign_func, 0, NULL, a->main_thread));
    clear_exception(&a->main_thread->exception);
    DECREF(foreign_func);

    object_t *function = create_function_object(a, NULL, 0, 0, dict, NULL, false);
    ASSERT(function && get_function_access(function)->process == a);
    DECREF(function);
    DECREF(container);
    DECREF(dict);
    DECREF(other);
    ASSERT(((array_object_t *)own)->refs == 1 && ((array_object_t *)foreign)->refs == 1);
    DECREF(own);
    DECREF(foreign);
    /* Never trace a deliberately invalid graph: every rejected operation left valid heaps. */
    destroy_process(a);
    destroy_process(b);
    ASSERT(get_allocated_memory_size() == initial);
    return true;
}
