/** @file test_operation_result.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Arithmetic result contracts, exception dispatch and ownership.
 */
#include "test_operation_result.h"

#include "codegen/linker.h"
#include "model/context.h"
#include "model/object.h"
#include "model/process.h"
#include "model/thread.h"
#include "test_macro.h"
#include "vm/gc.h"
#include "vm/vm.h"

#include <stdio.h>

static operation_result_t (*operations[])(process_t *, object_t *, object_t *) = {add_objects,
                                                                                  subtract_objects,
                                                                                  multiply_objects,
                                                                                  divide_objects,
                                                                                  modulo_objects,
                                                                                  power_objects};

static object_t *make_object(process_t *proc) {
    object_t *root = get_root_object();
    return create_user_defined_object(proc, (object_array_t){&root, 1});
}

static const opcode_t opcodes[] = {ADD, SUB, MUL, DIVIDE, MODULO, POWER};

static bytecode_t *make_code(const instruction_t *list, size_t count) {
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    for (size_t i = 0; i < count; i++)
        add_instruction(builder, list[i]);
    bytecode_t *code = link_code_and_data(builder, data);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    return code;
}

bool test_operation_results(void) {
    process_t *proc = create_process();
    const int expected[] = {10, 6, 16, 4, 0, 64};
    object_t *unsupported[] = {get_null_object(),
                               get_boolean_object(true),
                               get_function_print(),
                               get_exceptions_object(),
                               get_integer_proto(),
                               make_object(proc)};
    for (size_t i = 0; i < 6; i++) {
        operation_result_t result =
            operations[i](proc, get_static_integer_object(8), get_static_integer_object(2));
        ASSERT(!result.is_exception && result.value);
        ASSERT(get_object_real_value(result.value).value == expected[i]);
        DECREF(result.value);
        result = operations[i](proc, get_static_integer_object(8), get_null_object());
        ASSERT(result.is_exception && result.value == get_exception_invalid_argument());
        DECREF(result.value);
        for (size_t j = 0; j < sizeof(unsupported) / sizeof(*unsupported); j++) {
            result = operations[i](proc, unsupported[j], get_integer_zero());
            ASSERT(result.is_exception && result.value == get_exception_invalid_operation());
            DECREF(result.value);
        }
        object_t *real = create_real_number_object(proc, 8.5);
        result = operations[i](proc, real, get_static_integer_object(2));
        ASSERT(result.is_exception == (opcodes[i] == MODULO));
        ASSERT(result.value);
        DECREF(result.value);
        result = operations[i](proc, real, get_null_object());
        ASSERT(result.is_exception);
        ASSERT(result.value
               == (opcodes[i] == MODULO ? get_exception_invalid_operation()
                                        : get_exception_invalid_argument()));
        DECREF(result.value);
        DECREF(real);
    }
    object_t *numbers[] = {get_static_integer_object(8), create_real_number_object(proc, 8.5)};
    object_t *zeros[] = {get_integer_zero(), create_real_number_object(proc, -0.0)};
    for (size_t i = 0; i < 2; i++) {
        for (size_t j = 0; j < 2; j++) {
            operation_result_t result = divide_objects(proc, numbers[i], zeros[j]);
            ASSERT(result.is_exception && result.value == get_exception_division_by_zero());
            DECREF(result.value);
        }
    }
    operation_result_t result = modulo_objects(proc, get_static_integer_object(8), zeros[0]);
    ASSERT(result.is_exception && result.value == get_exception_division_by_zero());
    DECREF(result.value);
    object_t *min = create_integer_object(proc, INT64_MIN);
    result = modulo_objects(proc, min, get_static_integer_object(-1));
    ASSERT(!result.is_exception && result.value == get_integer_zero());
    DECREF(result.value);
    DECREF(min);
    for (size_t i = 0; i < 2; i++) {
        DECREF(numbers[i]);
        DECREF(zeros[i]);
    }
    for (size_t i = 0; i < sizeof(unsupported) / sizeof(*unsupported); i++)
        DECREF(unsupported[i]);
    destroy_process(proc);
    return true;
}

bool test_operation_vm_dispatch(void) {
    for (size_t op = 0; op < 6; op++) {
        for (int caught = 0; caught < 2; caught++) {
            /* Preserve 7 below TRY and discard 9 above it on an exception. */
            instruction_t list[] = {{.opcode = ILOAD32, .arg1 = 7},
                                    {.opcode = TRY, .arg1 = 8},
                                    {.opcode = ILOAD32, .arg1 = 9},
                                    {.opcode = ILOAD32, .arg1 = 8},
                                    {.opcode = NIL},
                                    {.opcode = opcodes[op]},
                                    {.opcode = END},
                                    {.opcode = END},
                                    {.opcode = END}};
            if (!caught)
                list[1].opcode = NOP;
            bytecode_t *code = make_code(list, sizeof(list) / sizeof(*list));
            process_t *proc = create_process();
            context_t *initial = proc->main_thread->context;
            ASSERT((run(proc, code) == 0) == !!caught);
            object_t *exception;
            if (caught) {
                ASSERT(proc->main_thread->context == initial);
                ASSERT(proc->main_thread->instr_id == 8);
                ASSERT(proc->main_thread->data_stack->size == 2);
                exception = pop_object_from_stack(proc->main_thread->data_stack);
                ASSERT(pop_object_from_stack(proc->main_thread->data_stack)
                       == get_static_integer_object(7));
                ASSERT(!proc->main_thread->exception.value);
                DECREF(exception);
            } else {
                ASSERT(proc->main_thread->data_stack->size == 0);
                exception = proc->main_thread->exception.value;
            }
            ASSERT(exception == get_exception_invalid_argument());
            destroy_process(proc);
            free_bytecode(code);
        }
    }
    return true;
}

static bool return_exception;
static size_t operand_releases;

static void record_release(object_t *obj) {
    operand_releases++;
}

/** @brief Returns an owned alias of the right operand, normally or exceptionally. */
static operation_result_t return_right(process_t *proc, object_t *left, object_t *right) {
    INCREF(right);
    return (operation_result_t){right, return_exception};
}

bool test_operation_result_ownership(void) {
    for (int exceptional = 0; exceptional < 2; exceptional++) {
        for (int caught = 0; caught < 2; caught++) {
            for (int null_value = 0; null_value < 2; null_value++) {
                process_t *proc = create_process();
                object_vtbl_t vtbl = *get_boolean_object(true)->vtbl;
                vtbl.add = return_right;
                vtbl.dec_ref = record_release;
                object_t left = {.vtbl = &vtbl};
                /* Establish the handler before loading operands onto the stack. */
                instruction_t setup[] = {{.opcode = TRY, .arg1 = 2},
                                         {.opcode = END},
                                         {.opcode = END}};
                if (!caught)
                    setup[0].opcode = NOP;
                bytecode_t *code = make_code(setup, 3);
                ASSERT(run(proc, code) == 0);
                free_bytecode(code);
                object_t *right = null_value ? get_null_object() : make_object(proc);
                push_object_onto_stack(proc->main_thread->data_stack, &left);
                push_object_onto_stack(proc->main_thread->data_stack, right);
                return_exception = exceptional;
                operand_releases = 0;
                instruction_t list[] = {{.opcode = ADD}, {.opcode = END}, {.opcode = END}};
                code = make_code(list, 3);
                proc->main_thread->instr_id = 0;
                ASSERT((run(proc, code) != 0) == (exceptional && !caught));
                ASSERT(operand_releases == 1);
                collect_garbage(proc);
                object_t *actual = exceptional && !caught
                                       ? proc->main_thread->exception.value
                                       : peek_object_from_stack(proc->main_thread->data_stack, 0);
                ASSERT(actual == right);
                ASSERT(actual->vtbl->type == (null_value ? TYPE_OTHER : TYPE_USER_DEFINED_OBJECT));
                destroy_process(proc);
                free_bytecode(code);
            }
        }
    }
    return true;
}
