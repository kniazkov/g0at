/**
 * @file vm.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Goat virtual machine.
 */

#include "vm.h"

#include "gc.h"
#include "lib/allocate.h"
#include "lib/avl_tree.h"
#include "lib/file_system.h"
#include "lib/split64.h"
#include "model/common_methods.h"
#include "model/context.h"
#include "model/module_file_declarations.h"
#include "model/module_files.h"
#include "model/thread.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** @brief The runtime environment for the Goat virtual machine. */
typedef struct {
    /** @brief Pointer to the bytecode being executed. */
    bytecode_t *code;
    /** @brief Standalone entry image when a context has no module-file owner. */
    bytecode_t *entry_code;
    /** @brief Current image owner and its independent descriptor cache. */
    module_files_t *files;
    /** @brief Nonzero if an instruction failed. */
    int status;
} runtime_t;

/**
 * @brief Typedef for functions that execute a single instruction.
 * @return A boolean value indicating whether the virtual machine should continue executing the next
 * instruction (`true`), or halt (`false`).
 */
typedef bool (*instr_executor_t)(runtime_t *runtime, instruction_t instr, thread_t *thread);

/** @brief Shared lifetime state; all executing threads have their own runtime_t. */
struct vm_execution_t {
    bytecode_t *entry_code;
    object_t *entry_scope; /**< Retained standalone entry scope until all threads complete. */
    uint32_t entry_file;
    bool reservations_done;
};

static bool dispatch_exception(runtime_t *runtime, thread_t *thread, exception_t exception);
static void execute_instructions(runtime_t *runtime, thread_t *thread, bool schedule);

/** @brief Loads a string from the bytecode or retrieves it from the cache. */
static object_t *load_string(runtime_t *runtime, process_t *process, uint32_t string_id) {
    if (runtime->files)
        return module_files_string(runtime->files, string_id);
    object_t *string = process->string_cache[string_id];
    if (string == NULL) {
        data_descriptor_t descriptor = runtime->code->data_descriptors[string_id];
        string = create_string_object(
            process,
            (string_value_t){(const gchar_t *)(runtime->code->data + descriptor.offset),
                             descriptor.size / sizeof(gchar_t) - 1,
                             false});
        process->string_cache[string_id] = string;
    }
    return string;
}

/** @brief Executes @ref NOP. */
static bool exec_NOP(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref ARG. */
static bool exec_ARG(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    if (thread->args_count == ARGS_CAPACITY) {
        return false; // bad bytecode
    }
    thread->args[thread->args_count++] = instr.arg1;
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref END. */
static bool exec_END(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return false;
}

/** @brief Executes @ref JUMP. */
static bool exec_JUMP(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    thread->instr_id = (instr_index_t)instr.arg1;
    return true;
}

/** @brief Executes @ref JIF. */
static bool exec_JIF(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    object_t *obj = pop_object_from_stack(thread->data_stack);
    bool flag = get_object_boolean_value(obj);
    if (flag) {
        thread->instr_id++;
    } else {
        thread->instr_id = (instr_index_t)instr.arg1;
    }
    DECREFIF(obj);
    return true;
}

/** @brief Executes @ref POP. */
static bool exec_POP(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    object_t *obj = pop_object_from_stack(thread->data_stack);
    DECREFIF(obj);
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref DUP. */
static bool exec_DUP(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    object_t *obj = peek_object_from_stack(thread->data_stack, 0);
    INCREF(obj);
    push_object_onto_stack(thread->data_stack, obj);
    thread->instr_id++;
    return true;
}

/** @brief Retains a source-ordered copy of a group of stack operands. */
static bool exec_DUPN(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    object_stack_t *stack = thread->data_stack;
    require_object_stack_size(stack, instr.arg1);
    size_t base = stack->size - instr.arg1;
    for (size_t i = 0; i < instr.arg1; i++) {
        object_t *value = stack->objects[base + i];
        INCREF(value);
        push_object_onto_stack(stack, value);
    }
    thread->instr_id++;
    return true;
}

/** @brief Places a result before retained address operands without changing ownership. */
static bool exec_ROTATE(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    object_stack_t *stack = thread->data_stack;
    require_object_stack_size(stack, 1);
    if (instr.arg1 > stack->size - 1)
        fail_stack_underflow();
    size_t base = stack->size - instr.arg1 - 1;
    object_t *value = stack->objects[stack->size - 1];
    memmove(stack->objects + base + 1, stack->objects + base, instr.arg1 * sizeof(*stack->objects));
    stack->objects[base] = value;
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref NIL. */
static bool exec_NIL(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    push_object_onto_stack(thread->data_stack, get_null_object());
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref TRUE. */
static bool exec_TRUE(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    push_object_onto_stack(thread->data_stack, get_boolean_object(true));
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref FALSE. */
static bool exec_FALSE(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    push_object_onto_stack(thread->data_stack, get_boolean_object(false));
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref ILOAD32. */
static bool exec_ILOAD32(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    int32_t value = (int32_t)instr.arg1;
    push_object_onto_stack(thread->data_stack, create_integer_object(thread->process, value));
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref ILOAD64. */
static bool exec_ILOAD64(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    if (thread->args_count != 1) {
        return false; // bad bytecode
    }
    split64_t s;
    s.parts[0] = thread->args[0];
    s.parts[1] = instr.arg1;
    push_object_onto_stack(thread->data_stack, create_integer_object(thread->process, s.int_value));
    thread->args_count = 0;
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref RLOAD. */
static bool exec_RLOAD(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    if (thread->args_count != 1) {
        return false; // bad bytecode
    }
    split64_t s;
    s.parts[0] = thread->args[0];
    s.parts[1] = instr.arg1;
    push_object_onto_stack(thread->data_stack,
                           create_real_number_object(thread->process, s.real_value));
    thread->args_count = 0;
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref SLOAD. */

static bool exec_SLOAD(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    uint32_t string_id = instr.arg1;
    if (string_id >= runtime->code->data_descriptor_count) {
        return false; // bad bytecode
    }
    object_t *string = load_string(runtime, thread->process, string_id);
    push_object_onto_stack(thread->data_stack, string);
    INCREF(string);
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref CLOAD. */
static bool exec_CLOAD(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    push_object_onto_stack(thread->data_stack,
                           create_character_object(thread->process, instr.arg1));
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref VLOAD. */
static bool exec_VLOAD(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    uint32_t string_id = instr.arg1;
    if (string_id >= runtime->code->data_descriptor_count) {
        return false; // bad bytecode
    }
    const data_descriptor_t *descriptor = runtime->code->data_descriptors + string_id;
    const gchar_t *name = (const gchar_t *)(runtime->code->data + descriptor->offset);
    object_t *constant = NULL;
    if (descriptor->count == 8 && !MEMCMP(name, U"__main__", 8)) {
        process_t *process = thread->process;
        constant = get_boolean_object(process->launch_module == thread->context->access.module
                                      && process->launch_file == thread->context->access.file_id);
    } else if (descriptor->count == 7 && !MEMCMP(name, U"__dir__", 7)) {
        constant = thread->process->launch_directory;
        if (!constant)
            constant = create_string_object(thread->process, (string_value_t){0});
    }
    if (constant) {
        INCREF(constant);
        push_object_onto_stack(thread->data_stack, constant);
        thread->instr_id++;
        return true;
    }
    object_t *key = load_string(runtime, thread->process, string_id);
    operation_result_t result =
        read_accessible_property(&thread->context->access, thread->context->data, key);
    if (result.is_exception)
        return dispatch_exception(
            runtime,
            thread,
            (exception_t){result.value, retain_exception_trace(result.trace)});
    push_object_onto_stack(thread->data_stack, result.value);
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref VAR. */
static bool exec_VAR(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    uint32_t string_id = instr.arg1;
    if (string_id >= runtime->code->data_descriptor_count) {
        return false; // bad bytecode
    }
    object_t *key = load_string(runtime, thread->process, string_id);
    object_t *value = pop_object_from_stack(thread->data_stack);
    field_cell_t *cell = get_own_field_cell(thread->context->data, key);
    bool reserved = cell && cell->pending_initialization && !cell->declaration_id
                    && cell->binding_unit == runtime->code->declaration_unit
                    && cell->is_constant == false;
    model_status_t result = reserved ? initialize_field_cell(thread->context->data, cell, value)
                                     : create_accessible_property(&thread->context->access,
                                                                  thread->context->data,
                                                                  key,
                                                                  value,
                                                                  false);
    DECREF(value);
    if (result != MSTAT_OK)
        return dispatch_exception(
            runtime,
            thread,
            (exception_t){
                access_status_result(result, module_scope_failure(thread->context->data)).value,
                result == MSTAT_FAILED_FILE
                    ? retain_exception_trace(
                          module_file_exception(module_scope_file(thread->context->data)).trace)
                    : NULL});
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref CONST. */
static bool exec_CONST(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    uint32_t string_id = instr.arg1;
    if (string_id >= runtime->code->data_descriptor_count) {
        return false; // bad bytecode
    }
    object_t *key = load_string(runtime, thread->process, string_id);
    object_t *value = pop_object_from_stack(thread->data_stack);
    field_cell_t *cell = get_own_field_cell(thread->context->data, key);
    bool reserved = cell && cell->pending_initialization && !cell->declaration_id
                    && cell->binding_unit == runtime->code->declaration_unit
                    && cell->is_constant == true;
    model_status_t result = reserved ? initialize_field_cell(thread->context->data, cell, value)
                                     : create_accessible_property(&thread->context->access,
                                                                  thread->context->data,
                                                                  key,
                                                                  value,
                                                                  true);
    DECREF(value);
    if (result != MSTAT_OK)
        return dispatch_exception(
            runtime,
            thread,
            (exception_t){
                access_status_result(result, module_scope_failure(thread->context->data)).value,
                result == MSTAT_FAILED_FILE
                    ? retain_exception_trace(
                          module_file_exception(module_scope_file(thread->context->data)).trace)
                    : NULL});
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref STORE. */
static bool exec_STORE(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    uint32_t string_id = instr.arg1;
    if (string_id >= runtime->code->data_descriptor_count) {
        return false; // bad bytecode
    }
    object_t *key = load_string(runtime, thread->process, string_id);
    object_t *value = peek_object_from_stack(thread->data_stack, 0);
    operation_result_t result = write_accessible_property_result(&thread->context->access,
                                                                 thread->context->data,
                                                                 key,
                                                                 value);
    if (result.is_exception)
        return dispatch_exception(
            runtime,
            thread,
            (exception_t){result.value, retain_exception_trace(result.trace)});
    DECREF(result.value);
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref GETPROP. */
static bool exec_GETPROP(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    uint32_t string_id = instr.arg1;
    if (string_id >= runtime->code->data_descriptor_count) {
        return false; // bad bytecode
    }
    object_t *key = load_string(runtime, thread->process, string_id);
    object_t *obj = pop_object_from_stack(thread->data_stack);
    operation_result_t result = read_accessible_property(&thread->context->access, obj, key);
    DECREF(obj);
    if (result.is_exception)
        return dispatch_exception(
            runtime,
            thread,
            (exception_t){result.value, retain_exception_trace(result.trace)});
    push_object_onto_stack(thread->data_stack, result.value);
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref SETPROP. */
static bool exec_SETPROP(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    uint32_t string_id = instr.arg1;
    if (string_id >= runtime->code->data_descriptor_count) {
        return false; // bad bytecode
    }
    object_t *key = load_string(runtime, thread->process, string_id);
    object_t *obj = pop_object_from_stack(thread->data_stack);
    object_t *value = peek_object_from_stack(thread->data_stack, 0);
    operation_result_t result =
        write_accessible_property_result(&thread->context->access, obj, key, value);
    DECREF(obj);
    if (result.is_exception)
        return dispatch_exception(
            runtime,
            thread,
            (exception_t){result.value, retain_exception_trace(result.trace)});
    DECREF(result.value);
    thread->instr_id++;
    return true;
}

/** @brief Preflight uses the same property policy and leaves the retained address untouched. */
static bool exec_write_check(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    if (instr.arg1 >= runtime->code->data_descriptor_count)
        return false;
    object_t *key = load_string(runtime, thread->process, instr.arg1);
    object_t *receiver = instr.opcode == CHECKVAR ? thread->context->data
                                                  : peek_object_from_stack(thread->data_stack, 0);
    operation_result_t result =
        check_accessible_property_write(&thread->context->access, receiver, key);
    if (result.is_exception)
        return dispatch_exception(
            runtime,
            thread,
            (exception_t){result.value, retain_exception_trace(result.trace)});
    DECREF(result.value);
    thread->instr_id++;
    return true;
}

/** @brief Validates a retained receiver and index tuple without reading the old value. */
static bool exec_CHECKINDEX(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    object_stack_t *stack = thread->data_stack;
    require_object_stack_size(stack, 1);
    if (instr.arg1 > stack->size - 1)
        fail_stack_underflow();
    size_t receiver_index = stack->size - instr.arg1 - 1;
    operation_result_t result = check_accessible_index_write(
        &thread->context->access,
        stack->objects[receiver_index],
        (object_array_t){stack->objects + receiver_index + 1, instr.arg1});
    if (result.is_exception)
        return dispatch_exception(
            runtime,
            thread,
            (exception_t){result.value, retain_exception_trace(result.trace)});
    DECREF(result.value);
    thread->instr_id++;
    return true;
}

/** @brief Reserved opcodes remain invalid even after later opcodes extend the table. */
static bool exec_reserved(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    runtime->status = 1;
    return false;
}

/** @brief Executes indexing with borrowed stack operands and an owned result. */
static bool execute_index(runtime_t *runtime, instruction_t instr, thread_t *thread, bool store) {
    object_stack_t *stack = thread->data_stack;
    size_t count = instr.arg1;
    /* Avoid overflowing count + receiver + value on 32-bit hosts. */
    require_object_stack_size(stack, store ? 2 : 1);
    if (count > stack->size - (store ? 2 : 1))
        fail_stack_underflow();
    size_t receiver_index = stack->size - count - 1;
    object_t *receiver = stack->objects[receiver_index];
    object_array_t indices = {stack->objects + receiver_index + 1, count};
    operation_result_t result = access_index(thread->process,
                                             &thread->context->access,
                                             receiver,
                                             indices,
                                             store ? stack->objects[receiver_index - 1] : NULL);
    size_t base = receiver_index - (store ? 1 : 0);
    while (stack->size > base) {
        object_t *operand = pop_object_from_stack(stack);
        DECREF(operand);
    }
    if (result.is_exception)
        return dispatch_exception(
            runtime,
            thread,
            (exception_t){result.value, retain_exception_trace(result.trace)});
    push_object_onto_stack(stack, result.value);
    thread->instr_id++;
    return true;
}

/** @brief Creates an array from the last arg1 stack values, preserving source order. */
static bool exec_ARRAY(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    object_stack_t *stack = thread->data_stack;
    require_object_stack_size(stack, instr.arg1);
    size_t base = stack->size - instr.arg1;
    object_t *array = create_array_object(
        thread->process,
        (object_array_t){instr.arg1 ? stack->objects + base : NULL, instr.arg1});
    while (stack->size > base) {
        object_t *value = pop_object_from_stack(stack);
        DECREF(value);
    }
    push_object_onto_stack(stack, array);
    thread->instr_id++;
    return true;
}

/** @brief Executes GETINDEX. */
static bool exec_GETINDEX(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_index(runtime, instr, thread, false);
}

/** @brief Executes SETINDEX. */
static bool exec_SETINDEX(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_index(runtime, instr, thread, true);
}

/** @brief Consumes one operand and transfers its result or exception. */
static bool execute_unary_operation(runtime_t *runtime,
                                    thread_t *thread,
                                    operation_result_t (*operation)(process_t *, object_t *)) {
    object_t *operand = pop_object_from_stack(thread->data_stack);
    operation_result_t result = operation(thread->process, operand);
    DECREF(operand);
    if (result.is_exception)
        return dispatch_exception(
            runtime,
            thread,
            (exception_t){result.value, retain_exception_trace(result.trace)});
    push_object_onto_stack(thread->data_stack, result.value);
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref INC. */
static bool exec_INC(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_unary_operation(runtime, thread, increment_object);
}

/** @brief Executes @ref DEC. */
static bool exec_DEC(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_unary_operation(runtime, thread, decrement_object);
}

/** @brief Executes @ref UPLUS. */
static bool exec_UPLUS(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_unary_operation(runtime, thread, unary_plus_object);
}

/** @brief Executes @ref UMINUS. */
static bool exec_UMINUS(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_unary_operation(runtime, thread, unary_minus_object);
}

/** @brief Consumes operands and transfers the result to the stack or exception handler. */
static bool
execute_binary_operation(runtime_t *runtime,
                         thread_t *thread,
                         operation_result_t (*operation)(process_t *, object_t *, object_t *)) {
    object_t *second = pop_object_from_stack(thread->data_stack);
    object_t *first = pop_object_from_stack(thread->data_stack);
    operation_result_t result =
        operation == add_objects
            ? add_objects_with_access(thread->process, first, second, &thread->context->access)
            : operation(thread->process, first, second);
    DECREF(first);
    DECREF(second);
    if (result.is_exception)
        return dispatch_exception(
            runtime,
            thread,
            (exception_t){result.value, retain_exception_trace(result.trace)});
    push_object_onto_stack(thread->data_stack, result.value);
    thread->instr_id++;
    return true;
}

/** @brief Consumes a value and pushes its truth value or negation. */
static bool boolean_unary(thread_t *thread, bool negate) {
    object_t *value = pop_object_from_stack(thread->data_stack);
    bool result = get_object_boolean_value(value) != negate;
    DECREF(value);
    push_object_onto_stack(thread->data_stack, get_boolean_object(result));
    thread->instr_id++;
    return true;
}

static bool exec_LNOT(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return boolean_unary(thread, true);
}

static bool exec_BOOL(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return boolean_unary(thread, false);
}

static bool exec_BNOT(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_unary_operation(runtime, thread, bitwise_not_object);
}

/** @brief Short-circuits with a boolean result, or leaves room for the right operand. */
static bool logical_jump(instruction_t instr, thread_t *thread, bool is_or) {
    object_t *value = pop_object_from_stack(thread->data_stack);
    bool truth = get_object_boolean_value(value);
    DECREF(value);
    if (truth == is_or) {
        push_object_onto_stack(thread->data_stack, get_boolean_object(truth));
        thread->instr_id = instr.arg1;
    } else
        thread->instr_id++;
    return true;
}

static bool exec_LAND(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return logical_jump(instr, thread, false);
}

static bool exec_LOR(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return logical_jump(instr, thread, true);
}

static bool exec_BAND(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, bitwise_and_objects);
}

static bool exec_BOR(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, bitwise_or_objects);
}

static bool exec_BXOR(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, bitwise_xor_objects);
}

static bool exec_SHL(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, shift_left_objects);
}

static bool exec_SHR(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, shift_right_objects);
}

/** @brief Executes @ref ADD. */
static bool exec_ADD(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, add_objects);
}

/** @brief Executes @ref SUB. */
static bool exec_SUB(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, subtract_objects);
}

/** @brief Executes @ref MUL. */
static bool exec_MUL(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, multiply_objects);
}

/** @brief Executes @ref DIVIDE. */
static bool exec_DIVIDE(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, divide_objects);
}

/** @brief Executes @ref MODULO. */
static bool exec_MODULO(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, modulo_objects);
}

/** @brief Executes @ref POWER. */
static bool exec_POWER(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, power_objects);
}

/** @brief Executes @ref LESS. */
static bool exec_LESS(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, is_object_less_than);
}

/** @brief Executes @ref LEQ. */
static bool exec_LEQ(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, is_object_less_or_equal);
}

/** @brief Executes @ref GREATER. */
static bool exec_GREATER(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, is_object_greater_than);
}

/** @brief Executes @ref GREQ. */
static bool exec_GREQ(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, is_object_greater_or_equal);
}

/** @brief Executes @ref EQUAL. */
static bool exec_EQUAL(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, are_objects_equal);
}

/** @brief Executes @ref DIFF. */
static bool exec_DIFF(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return execute_binary_operation(runtime, thread, are_objects_not_equal);
}

/** @brief Executes @ref FUNC. */
static bool exec_FUNC(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    object_t *closure = thread->context->data;
    if (thread->args_count < 1)
        fail_stack_underflow();
    instr_index_t first_instr_id = (instr_index_t)thread->args[0];
    uint16_t arg_count = instr.arg0;
    object_t **arg_names = NULL;
    if (arg_count > 0) {
        data_descriptor_t descriptor = runtime->code->data_descriptors[instr.arg1];
        if (arg_count * sizeof(uint32_t) != descriptor.size) {
            return false; // bad bytecode
        }
        const uint32_t *strings = (const uint32_t *)(runtime->code->data + descriptor.offset);
        arg_names = ALLOC(arg_count * sizeof(object_t *));
        for (uint16_t index = 0; index < arg_count; index++) {
            arg_names[index] = load_string(runtime, thread->process, strings[index]);
        }
    }
    object_t *function =
        create_function_object(thread->process,
                               arg_names,
                               arg_count,
                               first_instr_id,
                               closure,
                               get_bytecode_native_function(runtime->code, thread->instr_id),
                               thread->context->native_disabled);
    inherit_function_access(get_function_access(function), &thread->context->access);
    push_object_onto_stack(thread->data_stack, function);
    thread->args_count = 0;
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref CALL. */
static bool exec_CALL(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    require_object_stack_size(thread->data_stack, (size_t)instr.arg0 + 1);
    object_t *func = pop_object_from_stack(thread->data_stack);
    bool result = call_object(func, instr.arg0, NULL, thread);
    // The ID of the following instruction was set inside the call method
    DECREF(func);
    if (!result && thread->exception.value) {
        exception_t exception = thread->exception;
        thread->exception = (exception_t){0};
        return dispatch_exception(runtime, thread, exception);
    }
    if (!result)
        runtime->status = 1;
    return result;
}

/** @brief Executes @ref CALLM; the receiver sits below the function on the stack. */
static bool exec_CALLM(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    require_object_stack_size(thread->data_stack, (size_t)instr.arg0 + 2);
    object_t *func = pop_object_from_stack(thread->data_stack);
    object_t *receiver = pop_object_from_stack(thread->data_stack);
    bool result = call_object(func, instr.arg0, receiver, thread);
    // The ID of the following instruction was set inside the call method
    DECREF(func);
    DECREF(receiver);
    if (!result && thread->exception.value) {
        exception_t exception = thread->exception;
        thread->exception = (exception_t){0};
        return dispatch_exception(runtime, thread, exception);
    }
    if (!result)
        runtime->status = 1;
    return result;
}

/** @brief Drops temporaries above a saved context boundary. */
static void trim_to_context(thread_t *thread, context_t *ctx) {
    size_t size = ctx->unwinding_index == BAD_STACK_INDEX ? 0 : ctx->unwinding_index + 1;
    require_object_stack_size(thread->data_stack, size);
    while (thread->data_stack->size > size) {
        object_t *value = pop_object_from_stack(thread->data_stack);
        DECREF(value);
    }
}

/** @brief Takes ownership of a transfer value until cleanup finishes or replaces it. */
static bool start_cleanup(thread_t *thread,
                          context_t *ctx,
                          control_flow_t flow,
                          object_t *value,
                          instr_index_t address) {
    while (thread->context != ctx)
        thread->context = destroy_context(thread->context);
    trim_to_context(thread, ctx);
    ctx->control_flow = FLOW_CLEANUP;
    ctx->pending_flow = flow;
    ctx->pending_value = value;
    ctx->pending_address = address;
    thread->args_count = 0;
    thread->instr_id = ctx->jump_address[0];
    return true;
}

/** @brief Runs intervening cleanups before committing an owned return value. */
static bool return_value(runtime_t *runtime, thread_t *thread, object_t *ret_value) {
    context_t *ctx = thread->context;
    /* Validate the transfer before entering any finally. A top-level return is
       an exception at the return statement, so an enclosing catch must see it
       before its finally runs. Never cross an included file into its caller. */
    context_t *target = ctx;
    while (target && target->control_flow != FLOW_RETURN && !target->file_initialization_boundary)
        target = target->previous;
    if (!target || target->file_initialization_boundary) {
        DECREF(ret_value);
        return dispatch_exception(runtime,
                                  thread,
                                  (exception_t){.value = get_exception_return_outside_function()});
    }
    context_t *next = ctx;
    while (next != target) {
        if (next->control_flow == FLOW_FINALLY)
            return start_cleanup(thread, next, FLOW_RETURN, ret_value, BAD_INSTR_INDEX);
        next = next->previous;
    }
    assert(ctx->ret_value_index != BAD_STACK_INDEX);
    replace_object_on_stack(thread->data_stack, ret_value, ctx->ret_value_index);
    DECREF(ret_value);
    while (ctx && ctx->control_flow != FLOW_RETURN) {
        if (ctx->unwinding_index != BAD_STACK_INDEX) {
            reduce_object_stack(thread->data_stack, ctx->unwinding_index);
        }
        ctx = destroy_context(ctx);
    }
    assert(ctx != NULL);
    reduce_object_stack(thread->data_stack, ctx->unwinding_index);
    thread->instr_id = ctx->jump_address[0];
    thread->context = destroy_context(ctx);
    return true;
}

/** @brief Executes @ref RET. */
static bool exec_RET(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return return_value(runtime, thread, pop_object_from_stack(thread->data_stack));
}

/** @brief Executes @ref ENTER. */
static bool exec_ENTER(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    thread->context = create_context(thread->process, thread->context, NULL);
    thread->instr_id++;
    return true;
}

/** @brief Evaluates an explicit prototype list without allocating a Goat array. */
static bool exec_INHERIT(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    if (instr.arg0 > 1 || (!instr.arg0 && instr.arg1 != 1))
        return false;
    object_stack_t *stack = thread->data_stack;
    require_object_stack_size(stack, instr.arg1);
    size_t base = stack->size - instr.arg1;
    object_array_t prototypes = {instr.arg1 ? stack->objects + base : NULL, instr.arg1};
    if (!instr.arg0 && prototypes.items[0]->vtbl->type == TYPE_ARRAY)
        prototypes = get_array_elements(prototypes.items[0]);
    /* Like ordinary object blocks, the constructor retains its lexical environment.
       Explicit prototypes precede that fallback in the existing topological lookup. */
    if (prototypes.size > SIZE_MAX / sizeof(object_t *) - 1)
        abort();
    object_t **parents = ALLOC((prototypes.size + 1) * sizeof(*parents));
    for (size_t i = 0; i < prototypes.size; i++)
        parents[i] = prototypes.items[i];
    parents[prototypes.size] = thread->context->data;
    context_t *context = create_context_with_protos(thread->process,
                                                    thread->context,
                                                    (object_array_t){parents, prototypes.size + 1});
    FREE(parents);
    enable_prototype_shadowing(context->data);
    set_inheritance_prototype_count(context->data, prototypes.size);
    /* Only explicit inheritance grants protected rights. The dynamic receiver and lexical
       lookup topology of an ordinary call must never manufacture privileges. */
    for (size_t i = 0; i < prototypes.size; i++)
        add_protected_object_access(&context->access, prototypes.items[i]);
    context->native_disabled = true;
    while (stack->size > base) {
        object_t *value = pop_object_from_stack(stack);
        DECREF(value);
    }
    thread->context = context;
    thread->instr_id++;
    return true;
}

/** @brief Takes an owned snapshot before opening the loop scope. */
static bool exec_SNAPSHOT(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    object_t *source = pop_object_from_stack(thread->data_stack);
    exception_t failure = module_file_exception(module_scope_file(source));
    if (failure.value) {
        failure = retain_exception(failure);
        DECREF(source);
        return dispatch_exception(runtime, thread, failure);
    }
    object_t *snapshot =
        source->vtbl->type == TYPE_ARRAY
            ? create_array_object(thread->process, get_array_elements(source))
        : source->vtbl->type == TYPE_USER_DEFINED_OBJECT
            ? snapshot_object_keys_with_access(thread->process, source, &thread->context->access)
            : NULL;
    DECREF(source);
    if (!snapshot)
        return dispatch_exception(runtime,
                                  thread,
                                  (exception_t){.value = get_exception_invalid_operation()});
    push_object_onto_stack(thread->data_stack, snapshot);
    thread->instr_id++;
    return true;
}

/** @brief The snapshot stays below the loop stack boundary across continue. */
static bool exec_ITER_NEXT(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    object_t *snapshot = peek_object_from_stack(thread->data_stack, 0);
    if (snapshot->vtbl->type != TYPE_ARRAY || thread->context->control_flow != FLOW_LOOP
        || instr.arg1 >= runtime->code->instructions_count) {
        FPRINTF(stderr, "Invalid iteration bytecode.\n");
        runtime->status = 1;
        return false;
    }
    object_array_t elements = get_array_elements(snapshot);
    size_t index = thread->context->iteration_index;
    if (index >= elements.size) {
        thread->instr_id = instr.arg1;
    } else {
        thread->context->iteration_index++;
        INCREF(elements.items[index]);
        push_object_onto_stack(thread->data_stack, elements.items[index]);
        thread->instr_id++;
    }
    return true;
}

/** @brief Opens a scope that can be targeted by break and continue. */
static bool exec_LOOP_ENTER(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    thread->context = create_context(thread->process, thread->context, NULL);
    thread->context->control_flow = FLOW_LOOP;
    thread->context->unwinding_index =
        thread->data_stack->size ? thread->data_stack->size - 1 : BAD_STACK_INDEX;
    thread->instr_id++;
    return true;
}

/** @brief Discards block/try scopes and partial expression values, retaining the loop scope. */
static bool exec_LOOP_JUMP(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    if (instr.arg1 >= runtime->code->instructions_count)
        goto invalid;
    context_t *target = thread->context;
    while (target && target->control_flow != FLOW_LOOP) {
        if (target->control_flow == FLOW_RETURN || target->file_initialization_boundary)
            goto invalid;
        target = target->previous;
    }
    if (!target)
        goto invalid;
    for (context_t *ctx = thread->context; ctx != target; ctx = ctx->previous)
        if (ctx->control_flow == FLOW_FINALLY)
            return start_cleanup(thread, ctx, FLOW_LOOP, NULL, instr.arg1);
    if (target->unwinding_index == BAD_STACK_INDEX) {
        while (thread->data_stack->size) {
            object_t *value = pop_object_from_stack(thread->data_stack);
            DECREF(value);
        }
    } else {
        if (target->unwinding_index >= thread->data_stack->size)
            goto invalid;
        reduce_object_stack(thread->data_stack, target->unwinding_index);
    }
    while (thread->context != target)
        thread->context = destroy_context(thread->context);
    thread->instr_id = instr.arg1;
    return true;
invalid:
    FPRINTF(stderr, "Invalid loop transfer bytecode.\n");
    runtime->status = 1;
    return false;
}

/** @brief Executes @ref LEAVE. */
static bool exec_LEAVE(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    context_t *context = thread->context;
    if (context->file_initialization_boundary) {
        runtime->status = 1;
        return false;
    }
    object_t *object = context->data;
    INCREF(object);
    thread->context = destroy_context(context);
    push_object_onto_stack(thread->data_stack, object);
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref RESTORE without producing a block result. */
static bool exec_RESTORE(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    if (thread->context == get_root_context() || thread->context->file_initialization_boundary) {
        runtime->status = 1;
        return false;
    }
    thread->context = destroy_context(thread->context);
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref TRY, saving the stack boundary and handler address. */
static bool exec_TRY(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    if (instr.arg1 >= runtime->code->instructions_count) {
        runtime->status = 1;
        return false;
    }
    context_t *ctx = create_context(thread->process, thread->context, NULL);
    ctx->control_flow = FLOW_THROW;
    ctx->jump_address[0] = instr.arg1;
    ctx->unwinding_index =
        thread->data_stack->size ? thread->data_stack->size - 1 : BAD_STACK_INDEX;
    thread->context = ctx;
    thread->instr_id++;
    return true;
}

/** @brief Unwinds to a handler, transferring ownership of the thrown object. */
static bool dispatch_exception(runtime_t *runtime, thread_t *thread, exception_t exception) {
    if (!exception.trace)
        exception.trace = capture_exception_trace(thread->context, thread->instr_id);
    if (exception.value == get_exception_uninitialized_declaration()
        && module_initialization_active(thread->process)) {
        string_value_t chain = module_initialization_chain(thread->process, NULL);
        string_builder_t builder;
        init_string_builder(&builder, 0);
        append_static_string(&builder, U"UNINITIALIZED_DECLARATION: ");
        append_string_value(&builder, chain);
        FREE_STRING(chain);
        DECREF(exception.value);
        exception.value =
            create_string_object(thread->process,
                                 (string_value_t){builder.data, builder.length, true});
    }
    context_t *root = get_root_context();
    context_t *handler = thread->context;
    while (handler != root && !handler->file_initialization_boundary
           && handler->control_flow != FLOW_THROW && handler->control_flow != FLOW_FINALLY)
        handler = handler->previous;
    if (handler->control_flow == FLOW_FINALLY) {
        handler->pending_trace = exception.trace;
        return start_cleanup(thread, handler, FLOW_THROW, exception.value, BAD_INSTR_INDEX);
    }
    size_t size = handler == root || handler->unwinding_index == BAD_STACK_INDEX
                      ? 0
                      : handler->unwinding_index + 1;
    require_object_stack_size(thread->data_stack, size);
    while (thread->context != handler)
        thread->context = destroy_context(thread->context);
    while (thread->data_stack->size > size) {
        object_t *value = pop_object_from_stack(thread->data_stack);
        DECREF(value);
    }
    thread->args_count = 0;
    if (handler == root || handler->file_initialization_boundary) {
        clear_exception(&thread->exception);
        thread->exception = exception;
        runtime->status = 1;
        return false;
    }
    instr_index_t address = handler->jump_address[0];
    thread->context = destroy_context(handler);
    release_exception_trace(exception.trace);
    push_object_onto_stack(thread->data_stack, exception.value);
    thread->instr_id = address;
    return true;
}

/** @brief Executes @ref THROW; the top value survives stack and context unwinding. */
static bool exec_THROW(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    exception_t exception = {.value = pop_object_from_stack(thread->data_stack)};
    return dispatch_exception(runtime, thread, exception);
}

/** @brief Opens a protected scope with a cleanup address and stack boundary. */
static bool exec_FINALLY_ENTER(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    if (!exec_TRY(runtime, instr, thread))
        return false;
    thread->context->control_flow = FLOW_FINALLY;
    return true;
}

/** @brief Normal completion also visits cleanup exactly once. */
static bool exec_FINALLY_LEAVE(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    if (thread->context->control_flow != FLOW_FINALLY
        || instr.arg1 >= runtime->code->instructions_count) {
        runtime->status = 1;
        return false;
    }
    return start_cleanup(thread, thread->context, FLOW_NORMAL, NULL, instr.arg1);
}

/** @brief Removes cleanup before resuming the suspended transfer. */
static bool exec_FINALLY_END(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    context_t *ctx = thread->context;
    if (ctx->control_flow != FLOW_CLEANUP) {
        runtime->status = 1;
        return false;
    }
    control_flow_t flow = ctx->pending_flow;
    object_t *value = ctx->pending_value;
    const exception_trace_t *trace = ctx->pending_trace;
    instr_index_t address = ctx->pending_address;
    ctx->pending_value = NULL;
    ctx->pending_trace = NULL;
    trim_to_context(thread, ctx);
    thread->context = destroy_context(ctx);
    switch (flow) {
        case FLOW_RETURN:
            return return_value(runtime, thread, value);
        case FLOW_THROW:
            return dispatch_exception(runtime, thread, (exception_t){value, trace});
        case FLOW_LOOP:
            return exec_LOOP_JUMP(runtime, (instruction_t){.arg1 = address}, thread);
        case FLOW_NORMAL:
            thread->instr_id = address;
            return true;
        default:
            DECREFIF(value);
            runtime->status = 1;
            return false;
    }
}

/** @brief Declares a checked field with a file-local binding identity. */
static bool exec_declaration(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    declaration_descriptor_t d;
    if (!get_bytecode_declaration(runtime->code, instr.arg1, &d))
        return false;
    if (!runtime->code->declaration_unit)
        runtime->code->declaration_unit = create_module_identity();
    object_t *key = load_string(runtime, thread->process, d.name);
    object_t *value = pop_object_from_stack(thread->data_stack);
    object_t *storage = thread->context->data;
    bool constant = instr.opcode == DCONST;
    field_cell_t *cell = get_own_field_cell(storage, key);
    bool reserved = cell && cell->pending_initialization
                    && cell->binding_unit == runtime->code->declaration_unit
                    && cell->declaration_id == instr.arg1 + 1;
    model_status_t status =
        reserved
            ? initialize_field_cell(storage, cell, value)
            : create_accessible_property(&thread->context->access, storage, key, value, constant);
    if (status == MSTAT_OK && !reserved) {
        cell = get_own_field_cell(storage, key);
        cell->modifier = (access_modifier_t)d.modifier;
        cell->declaration_id = instr.arg1 + 1;
        cell->binding_unit = retain_module_identity(runtime->code->declaration_unit);
    }
    DECREF(value);
    if (status != MSTAT_OK)
        return dispatch_exception(
            runtime,
            thread,
            (exception_t){access_status_result(status, module_scope_failure(storage)).value,
                          status == MSTAT_FAILED_FILE
                              ? retain_exception_trace(
                                    module_file_exception(module_scope_file(storage)).trace)
                              : NULL});
    thread->instr_id++;
    return true;
}

/** @brief Bound reads/writes never resolve an identically named field on the receiver. */
static bool exec_binding(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    declaration_descriptor_t d;
    if (!get_bytecode_declaration(runtime->code, instr.arg1, &d))
        return false;
    property_lookup_t found = lookup_bound_property(&thread->context->access,
                                                    thread->context->data,
                                                    runtime->code->declaration_unit,
                                                    instr.arg1 + 1);
    bool check = instr.opcode == CHECKBOUND;
    bool write = instr.opcode == BSTORE || check;
    if (!write && found.status == MSTAT_PROPERTY_NOT_FOUND) {
        found.status = MSTAT_OK;
        found.value = get_null_object();
    }
    if (write && found.status == MSTAT_OK)
        found.status = check ? check_field_cell_write(found.storage, found.cell)
                             : set_field_cell_value(found.storage,
                                                    found.cell,
                                                    peek_object_from_stack(thread->data_stack, 0));
    if (found.status != MSTAT_OK)
        return dispatch_exception(
            runtime,
            thread,
            (exception_t){access_status_result(found.status, found.value).value,
                          found.status == MSTAT_FAILED_FILE
                              ? retain_exception_trace(
                                    module_file_exception(module_scope_file(found.storage)).trace)
                              : NULL});
    if (!write) {
        INCREF(found.value);
        push_object_onto_stack(thread->data_stack, found.value);
    }
    thread->instr_id++;
    return true;
}

/** @brief A synchronous initializer shares runtime control, not image-local caches. */
typedef struct {
    runtime_t *runtime;
    thread_t *thread;
    bool direct_entry;
} file_execution_t;

static bool
execute_module_file(module_file_t *file, context_t *frame, void *data, exception_t *failure) {
    file_execution_t *execution = data;
    runtime_t *runtime = execution->runtime;
    thread_t *thread = execution->thread;
    bytecode_t *caller_code = runtime->code;
    module_files_t *caller_files = runtime->files;
    context_t *caller = thread->context;
    instr_index_t address = thread->instr_id;
    uint32_t args[ARGS_CAPACITY];
    memcpy(args, thread->args, sizeof(args));
    int args_count = thread->args_count;
    int status = runtime->status;
    exception_t saved = thread->exception;
    thread->exception = (exception_t){0};
    size_t stack_size = thread->data_stack->size;
    frame->unwinding_index = stack_size ? stack_size - 1 : BAD_STACK_INDEX;
    thread->context = frame;
    thread->instr_id = module_file_entry(file);
    thread->args_count = 0;
    runtime->status = 0;
    /* Only the outer direct entry is ordinary program execution. A callback
       into that module while an include/import is active remains restricted. */
    bool initializer =
        thread->initialization_depth || frame->module != thread->process->launch_module
        || module_file_id(file) != thread->process->launch_file || !execution->direct_entry;
    if (initializer)
        thread->initialization_depth++;
    execute_instructions(runtime, thread, false);
    if (initializer)
        thread->initialization_depth--;
    bool success = runtime->status == 0 && !thread->exception.value;
    *failure = thread->exception;
    thread->exception = saved;
    while (thread->context != frame)
        thread->context = destroy_context(thread->context);
    trim_to_context(thread, frame);
    thread->context = caller;
    thread->instr_id = address;
    memcpy(thread->args, args, sizeof(args));
    thread->args_count = args_count;
    runtime->status = status;
    runtime->code = caller_code;
    runtime->files = caller_files;
    return success;
}

/** @brief Both module operators use the same persistent initialization gate. */
static bool execute_file_link(runtime_t *runtime, thread_t *thread, module_file_t *file) {
    if (!file)
        return dispatch_exception(runtime,
                                  thread,
                                  (exception_t){.value = get_exception_invalid_operation()});
    file_execution_t execution = {runtime, thread};
    object_t *scope = NULL;
    exception_t failure = {0};
    module_initialize_status_t status = initialize_module_file(file,
                                                               thread->context,
                                                               execute_module_file,
                                                               &execution,
                                                               &scope,
                                                               &failure);
    if (status != MODULE_INITIALIZE_OK) {
        if (!failure.value)
            failure.value = get_exception_invalid_operation();
        return dispatch_exception(runtime, thread, failure);
    }
    INCREF(scope);
    push_object_onto_stack(thread->data_stack, scope);
    thread->instr_id++;
    return true;
}

/** @brief Executes a fixed local-file link without opening any source path. */
static bool exec_MODULE_INCLUDE(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    module_files_t *files = find_module_files(thread->process, thread->context->module);
    return execute_file_link(runtime, thread, module_files_get(files, instr.arg1));
}

/** @brief Uses only the consumer's preflighted direct dependency binding. */
static bool exec_MODULE_IMPORT(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    module_files_t *files = find_module_files(thread->process, thread->context->module);
    return execute_file_link(runtime, thread, module_files_import(files, instr.arg1));
}

/** @brief Array of instruction execution functions for the Goat virtual machine. */
static instr_executor_t executors[] = {
    exec_NOP,      /**< No operation - does nothing. */
    exec_ARG,      /**< Argument push onto the argument stack. */
    exec_END,      /**< Ends the program immediately. */
    exec_JUMP,     /**< Unconditionally jumps to another instruction. */
    exec_JIF,      /**< Jumps to another instruction if the stack contains `false`. */
    exec_POP,      /**< Pops an object off the data stack. */
    exec_DUP,      /**< Duplicates the top stack value. */
    exec_NIL,      /**< Pushes a null object onto the data stack. */
    exec_TRUE,     /**< Pushes the boolean value true onto the data stack. */
    exec_FALSE,    /**< Pushes the boolean value false onto the data stack. */
    exec_ILOAD32,  /**< Pushes a 32-bit integer onto the data stack. */
    exec_ILOAD64,  /**< Pushes a 64-bit integer onto the data stack. */
    exec_RLOAD,    /**< Pushes a 64-bit float onto the data stack. */
    exec_SLOAD,    /**< Pushes a static string onto the data stack. */
    exec_CLOAD,    /**< Pushes a character code onto the data stack. */
    exec_VLOAD,    /**< Loads a variable value onto the data stack or `null` if undefined. */
    exec_VAR,      /**< Declares a new mutable variable in current context. */
    exec_CONST,    /**< Declares a new immutable constant in current context. */
    exec_STORE,    /**< Stores to existing variable or creates new if not found. */
    exec_GETPROP,  /**< Pushes the named property of the top object, or `null` if missing. */
    exec_SETPROP,  /**< Stores the value below the top into the named property of the top object. */
    exec_UPLUS,    /**< Applies unary plus to the top value. */
    exec_UMINUS,   /**< Negates the top value. */
    exec_INC,      /**< Applies numeric increment. */
    exec_DEC,      /**< Applies numeric decrement. */
    exec_LNOT,     /**< Negates truthiness. */
    exec_BOOL,     /**< Converts the top value to boolean. */
    exec_BNOT,     /**< Inverts integer bits. */
    exec_LAND,     /**< Short-circuits when the left value is false. */
    exec_LOR,      /**< Short-circuits when the left value is true. */
    exec_BAND,     /**< Computes integer bitwise AND. */
    exec_BOR,      /**< Computes integer bitwise OR. */
    exec_BXOR,     /**< Computes integer bitwise XOR. */
    exec_SHL,      /**< Shifts integer bits left. */
    exec_SHR,      /**< Shifts integer bits right with sign extension. */
    exec_ADD,      /**< Adds the top two objects of the stack. */
    exec_SUB,      /**< Subtracts the top two objects of the stack. */
    exec_MUL,      /**< Multiplies the top two objects on the data stack. */
    exec_DIVIDE,   /**< Divides the first object by the second on the data stack. */
    exec_MODULO,   /**< Computes the modulo of the top two objects on the data stack. */
    exec_POWER,    /**< Raises the first object to the power of the second. */
    exec_LESS,     /**< Checks if first < second and pushes boolean result. */
    exec_LEQ,      /**< Checks if first <= second and pushes boolean result. */
    exec_GREATER,  /**< Checks if first > second and pushes boolean result. */
    exec_GREQ,     /**< Checks if first >= second and pushes boolean result. */
    exec_EQUAL,    /**< Pushes `true` if top two objects are equal. */
    exec_DIFF,     /**< Pushes `true` if top two objects are not equal. */
    exec_FUNC,     /**< Creates a new function object. */
    exec_CALL,     /**< Calls a function with arguments from the data stack. */
    exec_CALLM,    /**< Calls a method, passing the receiver to the callee. */
    exec_RET,      /**< Returns from current function. */
    exec_ENTER,    /**< Creates a new context, inheriting from the current one. */
    exec_LEAVE,    /**< Restores the parent and pushes the departed context's data. */
    exec_RESTORE,  /**< Restores the parent without a stack result. */
    exec_TRY,      /**< Creates an exception-handler context. */
    exec_THROW,    /**< Unwinds to the nearest exception handler. */
    exec_GETINDEX, /**< Reads through the indexing virtual method. */
    exec_SETINDEX, /**< Writes through the indexing virtual method. */
    exec_ARRAY,    /**< Creates a new array. */
    exec_INHERIT,  /**< Enters an explicitly derived object. */
    exec_LOOP_ENTER,  exec_LOOP_JUMP,      exec_DUPN,          exec_ROTATE,
    exec_SNAPSHOT,    exec_ITER_NEXT,      exec_FINALLY_ENTER, exec_FINALLY_LEAVE,
    exec_FINALLY_END, exec_declaration,    exec_declaration,   exec_binding,
    exec_binding,     exec_MODULE_INCLUDE, exec_MODULE_IMPORT, exec_reserved,
    exec_reserved,    exec_write_check,    exec_CHECKINDEX,    exec_write_check,
    exec_binding
    // Additional opcodes can be added here in the future...
};

/** @brief Executes one thread; releases the model lock between instructions. */
static void execute_instructions(runtime_t *runtime, thread_t *thread, bool schedule) {
    (void)schedule;
    bool flag = true;
    while (flag && !thread->process->exit_requested) {
        /* Calls, returns, callbacks and exception unwinds all change lexical
           context. Resolve the image here rather than guessing at each edge. */
        if (thread->program_worker && thread->instr_id == BAD_INSTR_INDEX)
            break;
        runtime->files = find_module_files(thread->process, thread->context->module);
        runtime->code =
            runtime->files ? module_files_bytecode(runtime->files) : runtime->entry_code;
        if (thread->instr_id >= runtime->code->instructions_count
            || runtime->code->instructions[thread->instr_id].opcode
                   >= sizeof(executors) / sizeof(*executors)) {
            FPRINTF(stderr, "Invalid bytecode instruction address or opcode.\n");
            runtime->status = 1;
            break;
        }
        thread->context->instruction = thread->instr_id;
        instruction_t instr = runtime->code->instructions[thread->instr_id];
        flag = executors[instr.opcode](runtime, instr, thread);
        /* The initializer retains its own reentrant gate, not the model lock.
           Suspend all recursive entries, including an enclosing include handler. */
        size_t depth = process_execution_suspend(thread->process);
        process_execution_resume(thread->process, depth);
    }
}

/** @brief Completes one equal program thread, with the model lock held. */
static void complete_program_thread(thread_t *thread, int status) {
    thread->execution_status = status;
    thread->finished = true;
    assert(thread->process->active_threads);
    --thread->process->active_threads;
    broadcast_os_condition(thread->process->threads_changed);
}

/** @brief OS worker bootstrap reuses ordinary VM/native function dispatch. */
static void *program_thread_entry(void *data) {
    thread_t *thread = data;
    process_t *process = thread->process;
    process_execution_enter(process);
    runtime_t runtime = {.code = process->execution->entry_code,
                         .entry_code = process->execution->entry_code};
    /* A normal CALL increments or saves PC+1. This sentinel is the worker's
       ordinary function-return boundary, independent of the entry file. */
    thread->instr_id = BAD_INSTR_INDEX - 1;
    object_t *function = thread->start_function;
    thread->start_function = NULL;
    bool started = !process->exit_requested
                   && call_object(function, thread->start_argument_count, NULL, thread);
    DECREF(function);
    if (started)
        execute_instructions(&runtime, thread, false);
    else if (!process->exit_requested) {
        if (!thread->exception.value && !thread->native_status)
            thread->exception.value = get_exception_invalid_operation();
        runtime.status = 1;
    }
    complete_program_thread(thread, runtime.status);
    process_execution_leave(process);
    return NULL;
}

operation_result_t spawn_program_thread(thread_t *caller,
                                        object_t *callable,
                                        object_t *const *arguments,
                                        uint16_t count) {
    process_t *process = caller->process;
    if (caller->initialization_depth || !process->execution || process->exit_requested)
        return operation_exception(get_exception_invalid_operation());
    if (!callable || callable->vtbl->call == stub_call || (count && !arguments))
        return operation_exception(get_exception_invalid_argument());
    struct vm_execution_t *execution = process->execution;
    if (execution->entry_scope && !execution->reservations_done) {
        /* No competing program thread exists before this publication point.
           Preserve legacy single-thread declaration/shadowing behavior while
           ensuring the first observer sees every pending entry-file cell. */
        if (!prepare_remaining_file_scope_declarations(execution->entry_code,
                                                       execution->entry_file,
                                                       execution->entry_scope))
            return operation_exception(get_exception_invalid_operation());
        execution->reservations_done = true;
    }
    thread_t *thread = create_thread(process, create_context(process, get_root_context(), NULL));
    thread->program_worker = true;
    thread->start_function = callable;
    INCREF(callable);
    thread->start_argument_count = count;
    for (uint16_t i = 0; i < count; ++i) {
        INCREF(arguments[i]);
        push_object_onto_stack(thread->data_stack, arguments[i]);
    }
    ++process->active_threads;
    if (create_os_thread(&thread->worker, program_thread_entry, thread, 0) != OS_THREAD_OK) {
        --process->active_threads;
        destroy_thread(thread);
        return operation_exception(get_exception_invalid_operation());
    }
    return operation_success(create_integer_object(process, (int64_t)thread->id));
}

operation_result_t join_program_thread(thread_t *caller, uint64_t id) {
    process_t *process = caller->process;
    if (caller->initialization_depth || !process->execution)
        return operation_exception(get_exception_invalid_operation());
    thread_t *target = process->main_thread;
    do {
        if (target->id == id)
            break;
        target = target->next;
    } while (target != process->main_thread);
    if (target->id != id)
        return operation_exception(get_exception_invalid_argument());
    for (thread_t *cursor = target; cursor; cursor = cursor->waiting_for)
        if (cursor == caller)
            return operation_exception(get_exception_invalid_operation());
    caller->waiting_for = target;
    while (!target->finished && !process->exit_requested)
        wait_os_condition(process->threads_changed, process->execution_mutex);
    caller->waiting_for = NULL;
    if (process->exit_requested)
        return operation_success(get_null_object());
    if (target->exception.value) {
        target->failure_observed = true;
        INCREF(target->exception.value);
        return (operation_result_t){target->exception.value, true, target->exception.trace};
    }
    if (target->execution_status || target->native_status)
        return operation_exception(get_exception_invalid_operation());
    object_t *value = target->data_stack->size ? peek_object_from_stack(target->data_stack, 0)
                                               : get_null_object();
    INCREF(value);
    return operation_success(value);
}

/** @brief Wait for the last program thread, including descendants created meanwhile.
 * Records stay in the existing circular list for GC, diagnostics and join results.
 */
static int finish_program_threads(process_t *process, thread_t *entry, int status) {
    complete_program_thread(entry, status);
    while (process->active_threads)
        wait_os_condition(process->threads_changed, process->execution_mutex);
    if (entry->failure_observed && entry->exception.value && !entry->native_status)
        status = 0;
    thread_t *thread = process->main_thread;
    do {
        bool participated = thread == entry || thread->worker != NULL;
        if (thread->worker) {
            size_t depth = process_execution_suspend(process);
            join_os_thread(thread->worker, NULL);
            destroy_os_thread(thread->worker);
            process_execution_resume(process, depth);
            thread->worker = NULL;
        }
        if (participated && thread != entry
            && (thread->native_status
                || ((thread->execution_status || thread->exception.value)
                    && !thread->failure_observed))) {
            status = 1;
            if (!entry->exception.value && thread->exception.value) {
                entry->exception = retain_exception(thread->exception);
                thread->failure_observed = true;
            }
        }
        thread = thread->next;
    } while (thread != process->main_thread);
    DECREFIF(process->execution->entry_scope);
    process->execution = NULL;
    return status;
}

/** @brief Standalone-image fallback cache; module images own separate caches. */
static void prepare_string_cache(process_t *proc, bytecode_t *code) {
    proc->string_cache_size = code->data_descriptor_count;
    proc->string_cache =
        proc->string_cache_size ? CALLOC(proc->string_cache_size * sizeof(object_t *)) : NULL;
}

static void finish_string_cache(process_t *proc) {
    for (size_t index = 0; index < proc->string_cache_size; index++)
        DECREFIF(proc->string_cache[index]);
    FREE(proc->string_cache);
    proc->string_cache = NULL;
    proc->string_cache_size = 0;
    collect_garbage(proc);
}

int run(process_t *proc, bytecode_t *code) {
    process_execution_enter(proc);
    if (proc->execution) {
        process_execution_leave(proc);
        return 1;
    }
    struct vm_execution_t execution = {.entry_code = code};
    proc->execution = &execution;
    proc->active_threads = 1;
    proc->main_thread->finished = false;
    proc->main_thread->failure_observed = false;
    proc->main_thread->execution_status = 0;
    runtime_t runtime = {.code = code, .entry_code = code};
    prepare_string_cache(proc, code);
    thread_t *thread = proc->main_thread;
    if (thread->context->file_initialization_boundary)
        thread->context->unwinding_index =
            thread->data_stack->size ? thread->data_stack->size - 1 : BAD_STACK_INDEX;
    if (proc->launch_module == thread->context->module
        && proc->launch_file == thread->context->access.file_id && code->file_count) {
        execution.entry_scope = thread->context->data;
        execution.entry_file = proc->launch_file;
        INCREF(execution.entry_scope);
    }
    execute_instructions(&runtime, thread, true);
    int status = finish_program_threads(proc, thread, runtime.status);
    finish_string_cache(proc);
    int result = proc->exit_requested ? proc->exit_code : status;
    process_execution_leave(proc);
    return result;
}

int run_module_file(process_t *proc, module_file_t *file) {
    if (!proc || !file || module_file_process(file) != proc)
        return 1;
    if (proc->exit_requested)
        return proc->exit_code;
    process_execution_enter(proc);
    if (proc->execution) {
        process_execution_leave(proc);
        return 1;
    }
    bytecode_t *code = module_file_bytecode(file);
    struct vm_execution_t program = {.entry_code = code};
    proc->execution = &program;
    proc->active_threads = 1;
    proc->main_thread->finished = false;
    proc->main_thread->failure_observed = false;
    proc->main_thread->execution_status = 0;
    runtime_t runtime = {.code = code, .entry_code = code};
    thread_t *thread = proc->main_thread;
    file_execution_t execution = {&runtime, thread, true};
    object_t *scope = NULL;
    exception_t failure = {0};
    module_files_t *launch = find_module_files(proc, proc->launch_module);
    bool direct = launch && module_files_get(launch, proc->launch_file) == file;
    module_initialize_status_t status =
        (direct ? initialize_module_file_direct : initialize_module_file)(file,
                                                                          thread->context,
                                                                          execute_module_file,
                                                                          &execution,
                                                                          &scope,
                                                                          &failure);
    clear_exception(&thread->exception);
    thread->exception = failure;
    int result = finish_program_threads(proc, thread, status == MODULE_INITIALIZE_OK ? 0 : 1);
    finish_string_cache(proc);
    if (proc->exit_requested)
        result = proc->exit_code;
    process_execution_leave(proc);
    return result;
}
