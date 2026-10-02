/**
 * @file vm.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Goat virtual machine.
 */

#include "vm.h"

#include "gc.h"
#include "lib/allocate.h"
#include "lib/avl_tree.h"
#include "lib/split64.h"
#include "model/context.h"
#include "model/thread.h"

#include <assert.h>
#include <stdbool.h>

/** @brief The runtime environment for the Goat virtual machine. */
typedef struct {
    /** @brief Pointer to the bytecode being executed. */
    bytecode_t *code;
    /** @brief Nonzero if an instruction failed. */
    int status;
} runtime_t;

/**
 * @brief Typedef for functions that execute a single instruction.
 * @return A boolean value indicating whether the virtual machine should continue executing the next
 * instruction (`true`), or halt (`false`).
 */
typedef bool (*instr_executor_t)(runtime_t *runtime, instruction_t instr, thread_t *thread);

/**
 * @brief Retrieves the value of a property from an object or its prototypes.
 *
 * If the property is not found in the object or any of its prototypes, the function will return the
 * `null` object.
 * @return The value of the property, or the `null` object if the property was not found.
 */
static object_t *get_property_from_object_or_its_prototypes(object_t *obj, object_t *key) {
    object_t *value = get_object_property(obj, key);
    if (value == NULL) {
        object_array_t proto = get_object_topology(obj);
        size_t index = 0;
        do {
            value = get_object_property(proto.items[index], key);
            index++;
        } while (value == NULL && index < proto.size);
    }
    if (value == NULL) {
        value = get_null_object();
    }
    return value;
}

/** @brief Loads a string from the bytecode or retrieves it from the cache. */
static object_t *load_string(runtime_t *runtime, process_t *process, uint32_t string_id) {
    object_t *string = process->string_cache[string_id];
    if (string == NULL) {
        data_descriptor_t descriptor = runtime->code->data_descriptors[string_id];
        string = create_string_object(
            process,
            (string_value_t){(wchar_t *)(runtime->code->data + descriptor.offset),
                             descriptor.size / sizeof(wchar_t) - 1,
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

/** @brief Executes @ref VLOAD. */
static bool exec_VLOAD(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    uint32_t string_id = instr.arg1;
    if (string_id >= runtime->code->data_descriptor_count) {
        return false; // bad bytecode
    }
    object_t *key = load_string(runtime, thread->process, string_id);
    object_t *value = get_property_from_object_or_its_prototypes(thread->context->data, key);
    push_object_onto_stack(thread->data_stack, value);
    INCREF(value);
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
    model_status_t result = create_object_property(thread->context->data, key, value, false);
    if (result != MSTAT_OK) {
        return false; // already exists
    }
    DECREF(value);
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
    model_status_t result = create_object_property(thread->context->data, key, value, true);
    if (result != MSTAT_OK) {
        return false; // already exists
    }
    DECREF(value);
    thread->instr_id++;
    return true;
}

static bool dispatch_exception(runtime_t *runtime, thread_t *thread, exception_t exception);

/** @brief Executes @ref STORE. */
static bool exec_STORE(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    uint32_t string_id = instr.arg1;
    if (string_id >= runtime->code->data_descriptor_count) {
        return false; // bad bytecode
    }
    object_t *key = load_string(runtime, thread->process, string_id);
    object_t *value = peek_object_from_stack(thread->data_stack, 0);
    object_t *context = thread->context->data;
    bool changed = false;
    model_status_t result = set_object_property(context, key, value);
    assert(result != MSTAT_IMMUTABLE_OBJECT);
    if (result == MSTAT_PROPERTY_IS_CONSTANT)
        return dispatch_exception(runtime,
                                  thread,
                                  (exception_t){get_exception_property_is_constant()});
    if (result == MSTAT_OK) {
        changed = true;
    } else if (result == MSTAT_PROPERTY_NOT_FOUND) {
        object_array_t proto = get_object_topology(context);
        size_t index = 0;
        do {
            result = set_object_property(proto.items[index], key, value);
            if (result == MSTAT_PROPERTY_IS_CONSTANT)
                return dispatch_exception(runtime,
                                          thread,
                                          (exception_t){get_exception_property_is_constant()});
            if (result == MSTAT_IMMUTABLE_OBJECT) {
                break;
            }
            if (result == MSTAT_OK) {
                changed = true;
                break;
            }
            index++;
        } while (index < proto.size);
    }
    if (!changed) {
        result = create_object_property(context, key, value, false);
        if (result != MSTAT_OK) {
            return false;
        }
    }
    thread->instr_id++;
    return true;
}

/** @brief Consumes one operand and transfers its result or exception. */
static bool execute_unary_operation(runtime_t *runtime,
                                    thread_t *thread,
                                    operation_result_t (*operation)(process_t *, object_t *)) {
    object_t *operand = pop_object_from_stack(thread->data_stack);
    operation_result_t result = operation(thread->process, operand);
    DECREF(operand);
    if (result.is_exception)
        return dispatch_exception(runtime, thread, (exception_t){result.value});
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
    operation_result_t result = operation(thread->process, first, second);
    DECREF(first);
    DECREF(second);
    if (result.is_exception)
        return dispatch_exception(runtime, thread, (exception_t){result.value});
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
        uint32_t *strings = (uint32_t *)(runtime->code->data + descriptor.offset);
        arg_names = ALLOC(arg_count * sizeof(object_t *));
        for (uint16_t index = 0; index < arg_count; index++) {
            arg_names[index] = load_string(runtime, thread->process, strings[index]);
        }
    }
    object_t *function =
        create_function_object(thread->process, arg_names, arg_count, first_instr_id, closure);
    push_object_onto_stack(thread->data_stack, function);
    thread->args_count = 0;
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref CALL. */
static bool exec_CALL(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    require_object_stack_size(thread->data_stack, (size_t)instr.arg0 + 1);
    object_t *func = pop_object_from_stack(thread->data_stack);
    bool result = call_object(func, instr.arg0, thread);
    // The ID of the following instruction was set inside the call method
    DECREF(func);
    return result;
}

/** @brief Executes @ref RET. */
static bool exec_RET(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    context_t *ctx = thread->context;
    require_object_stack_size(thread->data_stack, 1);
    assert(ctx->ret_value_index != BAD_STACK_INDEX);
    object_t *ret_value = pop_object_from_stack(thread->data_stack);
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

/** @brief Executes @ref ENTER. */
static bool exec_ENTER(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    thread->context = create_context(thread->process, thread->context, NULL);
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref LEAVE. */
static bool exec_LEAVE(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    context_t *context = thread->context;
    object_t *object = context->data;
    INCREF(object);
    thread->context = destroy_context(context);
    push_object_onto_stack(thread->data_stack, object);
    thread->instr_id++;
    return true;
}

/** @brief Executes @ref RESTORE without producing a block result. */
static bool exec_RESTORE(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    if (thread->context == get_root_context()) {
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
    context_t *root = get_root_context();
    context_t *handler = thread->context;
    while (handler != root && handler->control_flow != FLOW_THROW)
        handler = handler->previous;
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
    if (handler == root) {
        DECREFIF(thread->exception.value);
        thread->exception = exception;
        runtime->status = 1;
        return false;
    }
    instr_index_t address = handler->jump_address[0];
    thread->context = destroy_context(handler);
    push_object_onto_stack(thread->data_stack, exception.value);
    thread->instr_id = address;
    return true;
}

/** @brief Executes @ref THROW; the top value survives stack and context unwinding. */
static bool exec_THROW(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    exception_t exception = {pop_object_from_stack(thread->data_stack)};
    return dispatch_exception(runtime, thread, exception);
}

/** @brief Array of instruction execution functions for the Goat virtual machine. */
static instr_executor_t executors[] = {
    exec_NOP,     /**< No operation - does nothing. */
    exec_ARG,     /**< Argument push onto the argument stack. */
    exec_END,     /**< Ends the program immediately. */
    exec_JUMP,    /**< Unconditionally jumps to another instruction. */
    exec_JIF,     /**< Jumps to another instruction if the stack contains `false`. */
    exec_POP,     /**< Pops an object off the data stack. */
    exec_DUP,     /**< Duplicates the top stack value. */
    exec_NIL,     /**< Pushes a null object onto the data stack. */
    exec_TRUE,    /**< Pushes the boolean value true onto the data stack. */
    exec_FALSE,   /**< Pushes the boolean value false onto the data stack. */
    exec_ILOAD32, /**< Pushes a 32-bit integer onto the data stack. */
    exec_ILOAD64, /**< Pushes a 64-bit integer onto the data stack. */
    exec_RLOAD,   /**< Pushes a 64-bit float onto the data stack. */
    exec_SLOAD,   /**< Pushes a static string onto the data stack. */
    exec_VLOAD,   /**< Loads a variable value onto the data stack or `null` if undefined. */
    exec_VAR,     /**< Declares a new mutable variable in current context. */
    exec_CONST,   /**< Declares a new immutable constant in current context. */
    exec_STORE,   /**< Stores to existing variable or creates new if not found. */
    exec_UPLUS,   /**< Applies unary plus to the top value. */
    exec_UMINUS,  /**< Negates the top value. */
    exec_INC,     /**< Applies numeric increment. */
    exec_DEC,     /**< Applies numeric decrement. */
    exec_LNOT,    /**< Negates truthiness. */
    exec_BOOL,    /**< Converts the top value to boolean. */
    exec_BNOT,    /**< Inverts integer bits. */
    exec_LAND,    /**< Short-circuits when the left value is false. */
    exec_LOR,     /**< Short-circuits when the left value is true. */
    exec_BAND,    /**< Computes integer bitwise AND. */
    exec_BOR,     /**< Computes integer bitwise OR. */
    exec_BXOR,    /**< Computes integer bitwise XOR. */
    exec_SHL,     /**< Shifts integer bits left. */
    exec_SHR,     /**< Shifts integer bits right with sign extension. */
    exec_ADD,     /**< Adds the top two objects of the stack. */
    exec_SUB,     /**< Subtracts the top two objects of the stack. */
    exec_MUL,     /**< Multiplies the top two objects on the data stack. */
    exec_DIVIDE,  /**< Divides the first object by the second on the data stack. */
    exec_MODULO,  /**< Computes the modulo of the top two objects on the data stack. */
    exec_POWER,   /**< Raises the first object to the power of the second. */
    exec_LESS,    /**< Checks if first < second and pushes boolean result. */
    exec_LEQ,     /**< Checks if first <= second and pushes boolean result. */
    exec_GREATER, /**< Checks if first > second and pushes boolean result. */
    exec_GREQ,    /**< Checks if first >= second and pushes boolean result. */
    exec_EQUAL,   /**< Pushes `true` if top two objects are equal. */
    exec_DIFF,    /**< Pushes `true` if top two objects are not equal. */
    exec_FUNC,    /**< Creates a new function object. */
    exec_CALL,    /**< Calls a function with arguments from the data stack. */
    exec_RET,     /**< Returns from current function. */
    exec_ENTER,   /**< Creates a new context, inheriting from the current one. */
    exec_LEAVE,   /**< Restores the parent and pushes the departed context's data. */
    exec_RESTORE, /**< Restores the parent without a stack result. */
    exec_TRY,     /**< Creates an exception-handler context. */
    exec_THROW    /**< Unwinds to the nearest exception handler. */
    // Additional opcodes can be added here in the future...
};

int run(process_t *proc, bytecode_t *code) {
    // preparing the environment
    runtime_t runtime;
    runtime.code = code;
    runtime.status = 0;
    if ((proc->string_cache_size = code->data_descriptor_count) > 0) {
        proc->string_cache = CALLOC(code->data_descriptor_count * sizeof(object_t *));
    }

    // execution
    bool flag = true;
    thread_t *thread = proc->main_thread;
    while (flag) {
        instruction_t instr = code->instructions[thread->instr_id];
        instr_executor_t exec = executors[instr.opcode];
        flag = exec(&runtime, instr, thread);
        thread = thread->next;
    }

    // cleanup
    for (size_t index = 0; index < code->data_descriptor_count; index++) {
        DECREFIF(proc->string_cache[index]);
    }
    FREE(proc->string_cache);
    proc->string_cache = NULL;
    proc->string_cache_size = 0;
    collect_garbage(proc);
    return runtime.status;
}
