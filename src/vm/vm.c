/**
 * @file vm.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Goat virtual machine.
 */

#include <assert.h>
#include <stdbool.h>

#include "vm.h"
#include "gc.h"
#include "model/context.h"
#include "model/thread.h"
#include "lib/allocate.h"
#include "lib/avl_tree.h"
#include "lib/split64.h"

/**
 * @struct runtime_t
 * @brief Structure to represent the runtime environment for the Goat virtual machine.
 * 
 * This structure holds all the data and state required for the execution of the bytecode 
 * within the virtual machine. It includes the bytecode and other runtime-related information
 * necessary for the program's execution.
 */
typedef struct {
    /**
     * @brief Pointer to the bytecode being executed.
     */
    bytecode_t *code;
} runtime_t;

/**
 * @typedef instr_executor_t
 * @brief Typedef for functions that execute a single instruction.
 * 
 * This typedef defines the function signature for functions that execute a single instruction
 * in the Goat virtual machine. These functions take an instruction to be executed, the current
 * thread, and the bytecode that the thread is executing. If the function returns `true`, 
 * the virtual machine proceeds to the next instruction. If the function returns `false`, 
 * the virtual machine halts execution.
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return A boolean value indicating whether the virtual machine should continue 
 *  executing the next instruction (`true`), or halt (`false`).
 */
typedef bool (*instr_executor_t)(runtime_t *runtime, instruction_t instr, thread_t *thread);

/**
 * @brief Retrieves the value of a property from an object or its prototypes.
 * 
 * This function attempts to retrieve the value of a property identified by the `key` from
 * the specified `obj`. If the property is not found directly on the object, it will search through
 * the object's prototypes (as defined by the object's prototype chain) until the property is found
 * or the end of the chain is reached.
 * 
 * If the property is not found in the object or any of its prototypes, the function will return
 * the `null` object.
 * 
 * @param obj The object from which to retrieve the property.
 * @param key The key identifying the property to retrieve.
 * @return The value of the property, or the `null` object if the property was not found.
 */
static object_t *get_property_from_object_or_its_prototypes(object_t *obj, object_t *key) {
    object_t *value = obj->vtbl->get_property(obj, key);
    if (value == NULL) {
        object_array_t proto = obj->vtbl->get_topology(obj);
        size_t index = 0;
        do {
            value = proto.items[index]->vtbl->get_property(proto.items[index], key);
            index++;
        } while (value == NULL && index < proto.size);
    }
    if (value == NULL) {
        value = get_null_object();
    }
    return value;
}

/**
 * @brief Loads a string from the bytecode or retrieves it from the cache.
 * @param runtime The runtime environment containing the static data cache and bytecode.
 * @param string_id The identifier of the static string to load.
 * @return A pointer to the `object_t` representing the static string.
 */
static object_t *load_string(runtime_t *runtime, process_t *process, uint32_t string_id) {
    object_t *string = process->string_cache[string_id];
    if (string == NULL) {
        data_descriptor_t descriptor = runtime->code->data_descriptors[string_id];
        string = create_string_object(
            process,
            (string_value_t) {
                (wchar_t*)(runtime->code->data + descriptor.offset),
                descriptor.size / sizeof(wchar_t) - 1,
                false
            }
        );
        process->string_cache[string_id] = string;
    }
    return string;
}

/**
 * @brief Executes the NOP instruction.
 * 
 * The `NOP` opcode performs no operation and simply advances the instruction pointer.
 * It is typically used as a placeholder or for debugging.
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Always returns `true` to continue executing the next instruction.
 */
static bool exec_NOP(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    thread->instr_id++;
    return true;
}

/**
 * @brief Executes the ARG instruction.
 * 
 * The `ARG` opcode pushes an argument onto the argument stack. If the argument stack is full,
 * it returns `false`. This instruction is used for pushing additional arguments onto the stack
 * for instructions that require more than two arguments.
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return `true` if the argument was successfully pushed, `false` if the stack is full.
 */
static bool exec_ARG(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    if (thread->args_count == ARGS_CAPACITY) {
        return false; // bad bytecode
    }
    thread->args[thread->args_count++] = instr.arg1;
    thread->instr_id++;
    return true;
}

/**
 * @brief Executes the END instruction.
 * 
 * The `END` opcode signals the end of the program, causing the virtual machine to halt
 * execution. Once this instruction is encountered, no further instructions will be executed.
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Always returns `false` to terminate the execution.
 */
static bool exec_END(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return false;
}

/**
 * @brief Executes the POP instruction.
 * 
 * The `POP` opcode removes the topmost object from the data stack. It is used when an object
 * is no longer needed and should be discarded from the stack.
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Always returns `true` to continue executing the next instruction.
 */
static bool exec_POP(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    object_t *obj = pop_object_from_stack(thread->data_stack);
    DECREFIF(obj);
    thread->instr_id++;
    return true;
}

/**
 * @brief Executes the NIL instruction.
 * 
 * The `NIL` opcode pushes a null object onto the data stack. This is used to represent
 * the absence of a value or as a default placeholder. The null object is a singleton
 * instance that can be referenced multiple times without needing additional allocations.
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute (unused in this operation).
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Always returns `true` to continue executing the next instruction.
 */
static bool exec_NIL(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    push_object_onto_stack(thread->data_stack, get_null_object());
    thread->instr_id++;
    return true;
}

/**
 * @brief Executes the ILOAD32 instruction.
 * 
 * The `ILOAD32` opcode pushes a 32-bit integer onto the data stack. This operation loads a
 * 32-bit integer value from the instruction argument and pushes it onto the stack.
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Always returns `true` to continue executing the next instruction.
 */
static bool exec_ILOAD32(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    int32_t value = (int32_t)instr.arg1;
    push_object_onto_stack(thread->data_stack, create_integer_object(thread->process, value));
    thread->instr_id++;
    return true;
}

/**
 * @brief Executes the ILOAD64 instruction.
 * 
 * The `ILOAD64` opcode pushes a 64-bit integer onto the data stack. Since 64-bit integers
 * are pushed in two parts (high and low), this instruction uses the argument stack to combine
 * the 32-bit parts and then pushes the resulting 64-bit integer onto the stack.
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return `true` if the value was successfully pushed, `false` if the argument stack is not
 *  properly set up.
 */
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

/**
 * @brief Executes the SLOAD opcode to load a static string into the stack.
 *
 * The `SLOAD` opcode loads a static string identified by its `string_id` into the stack. The
 * staticstrings are pre-defined in the bytecode and are loaded lazily upon the first access.
 * If the string has already been loaded, it is retrieved from the cache. If not, it is created
 * from the bytecode data and stored in the cache for future use.
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * 
 * @return `true` if the string was successfully loaded and pushed onto the stack, `false` if there
 *  is an error (e.g., invalid string id or bytecode corruption).
 */

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

/**
 * @brief Executes the VLOAD opcode to load a variable value from the context.
 * 
 * The `VLOAD` opcode loads a variable value from the current context based on the `string_id`,
 * which corresponds to the variable's name. The function retrieves the property identified by 
 * the `string_id` from the context's data object. If the property does not exist, it loads 
 * `null` as a placeholder. The value is then pushed onto the stack.
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return `true` if the variable value was successfully loaded and pushed onto the stack,
 *  `false` if there is an error (e.g., invalid string id or bytecode corruption).
 */
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

/**
 * @brief Executes the VAR opcode to declare a mutable variable in current context.
 *
 * The `VAR` opcode declares a new mutable variable in the current execution context.
 * It pops the initial value from the stack and associates it with the variable name.
 * The operation will fail if:
 * - The string_id is invalid (bytecode corruption)
 * - The stack is empty
 * - A variable or constant with this name already exists in current context
 *
 * @param runtime The runtime environment.
 * @param instr The instruction containing the string_id argument.
 * @param thread The thread executing the instruction.
 * @return `true` if variable was successfully declared, `false` on any error.
 */
static bool exec_VAR(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    uint32_t string_id = instr.arg1;
    if (string_id >= runtime->code->data_descriptor_count) {
        return false; // bad bytecode
    }
    object_t *key = load_string(runtime, thread->process, string_id);
    object_t *value = pop_object_from_stack(thread->data_stack);
    if (value == NULL) {
        return false; // empty stack
    }
    model_status_t result = thread->context->data->vtbl->add_property(thread->context->data,
            key, value, false);
    if (result != MSTAT_OK) {
        return false; // already exists
    }
    DECREF(value);
    thread->instr_id++;
    return true;
}

/**
 * @brief Executes the CONST opcode to declare an immutable constant in current context.
 *
 * The `CONST` opcode declares a new immutable constant in the current execution context.
 * It pops the initial value from the stack and associates it with the constant name.
 * The operation will fail if:
 * - The string_id is invalid (bytecode corruption)
 * - The stack is empty
 * - A variable or constant with this name already exists in current context
 *
 * @param runtime The runtime environment.
 * @param instr The instruction containing the string_id argument.
 * @param thread The thread executing the instruction.
 * @return `true` if variable was successfully declared, `false` on any error.
 */
static bool exec_CONST(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    uint32_t string_id = instr.arg1;
    if (string_id >= runtime->code->data_descriptor_count) {
        return false; // bad bytecode
    }
    object_t *key = load_string(runtime, thread->process, string_id);
    object_t *value = pop_object_from_stack(thread->data_stack);
    if (value == NULL) {
        return false; // empty stack
    }
    model_status_t result = thread->context->data->vtbl->add_property(thread->context->data,
            key, value, true);
    if (result != MSTAT_OK) {
        return false; // already exists
    }
    DECREF(value);
    thread->instr_id++;
    return true;
}

/**
 * @brief Executes the STORE opcode to update a variable value.
 *
 * The `STORE` opcode updates an existing variable's value by searching through:
 * 1. Current context's variables
 * 2. Prototype chain (parent contexts)
 * If the variable is not found, it creates a new mutable variable in current context.
 * The operation will fail if:
 * - The string_id is invalid
 * - The stack is empty
 *
 * @param runtime The runtime environment.
 * @param instr The instruction containing the string_id argument.
 * @param thread The thread executing the instruction.
 * @return `true` if variable was successfully declared, `false` on any error.
 */
static bool exec_STORE(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    uint32_t string_id = instr.arg1;
    if (string_id >= runtime->code->data_descriptor_count) {
        return false; // bad bytecode
    }
    object_t *key = load_string(runtime, thread->process, string_id);
    object_t *value = peek_object_from_stack(thread->data_stack, 0);
    if (value == NULL) {
        return false; // empty stack
    }
    object_t *context = thread->context->data;
    bool changed = false;
    model_status_t result = context->vtbl->set_property(context, key, value);
    assert(result != MSTAT_IMMUTABLE_OBJECT);
    if (result == MSTAT_OK) {
        changed = true;
    }
    else if (result == MSTAT_PROPERTY_NOT_FOUND) {
        object_array_t proto = context->vtbl->get_topology(context);
        size_t index = 0;
        do {
            result = proto.items[index]->vtbl->set_property(proto.items[index], key, value);
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
        result = context->vtbl->add_property(context, key, value, false);
        if (result != MSTAT_OK) {
            return false;
        }
    }
    thread->instr_id++;
    return true;
}

/**
 * @brief Executes the `ADD` instruction.
 * 
 * The `ADD` opcode performs an addition operation on the top two objects on the data stack.
 * It pops the top two objects from the stack, adds them using their respective `add` methods,
 * and pushes the result back onto the stack. If either of the objects cannot be added, the
 * operation fails.
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Returns `true` if the addition was successful and the result was pushed onto the stack,
 *  or `false` if the addition failed (e.g., due to invalid object types).
 */
static bool exec_ADD(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    object_t *second = pop_object_from_stack(thread->data_stack);
    object_t *first = pop_object_from_stack(thread->data_stack);
    if (first && second) {
        object_t *result = first->vtbl->add(thread->process, first, second);
        if (result) {
            DECREF(first);
            DECREF(second);
            push_object_onto_stack(thread->data_stack, result);
            thread->instr_id++;
            return true;
        }
    }
    return false;
}

/**
 * @brief Executes the `SUB` instruction.
 * 
 * The `SUB` opcode performs a subtraction operation on the top two objects on the data stack.
 * It pops the top two objects from the stack, subtracts the second operand from the first using
 * their respective `sub` methods, and pushes the result back onto the stack. If either of the
 * objects cannot be subtracted, the operation fails.
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Returns `true` if the subtraction was successful and the result was pushed onto the
 *  stack, or `false` if the subtraction failed (e.g., due to invalid object types).
 */
static bool exec_SUB(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    object_t *second = pop_object_from_stack(thread->data_stack);
    object_t *first = pop_object_from_stack(thread->data_stack);
    if (first && second) {
        object_t *result = first->vtbl->sub(thread->process, first, second);
        if (result) {
            DECREF(first);
            DECREF(second);
            push_object_onto_stack(thread->data_stack, result);
            thread->instr_id++;
            return true;
        }
    }
    return false;
}

/**
 * @brief Executes the `FUNC` instruction.
 * 
 * The `FUNC` opcode creates a new function object using arguments:
 * - Immediate 16-bit argument count (instr.arg0)
 * - 32-bit argument names reference (instr.arg1)
 * - 32-bit entry point address (from argument stack)
 * 
 * Upon execution, this function:
 * - Pops the entry point address from the argument stack
 * - Creates a new function object with:
 *   - Specified parameter count
 *   - Argument names from data segment
 *   - Entry point for execution
 * - Pushes the created function object onto the data stack
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute. The `arg0` field contains the argument count,
 *              and `arg1` contains the argument names reference.
 * @param thread Pointer to the executing thread.
 * @return Returns `true` if the function object was successfully created and pushed,
 *         or `false` if creation failed (e.g., invalid arguments).
 */
static bool exec_FUNC(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return false;
}

/**
 * @brief Executes the `CALL` instruction.
 * 
 * The `CALL` opcode interprets the object at the top of the data stack as a function and invokes
 * it. It expects the specified number of arguments (provided in the `arg0` field of the
 * instruction) to already be present on the stack in the correct order.
 * 
 * Upon execution, this function:
 * - Pops the function object from the data stack.
 * - Invokes the function using its `call` method, passing the specified number of arguments
 *   from the stack and the current thread as parameters.
 * - Handles the result of the function invocation:
 *   - If the function returns a value, the result is pushed onto the data stack.
 *   - If the function does not return a value, a `null` object is pushed onto the stack.
 * 
 * If the function cannot be invoked (e.g., the object is not callable, insufficient arguments
 * are on the stack, or other runtime errors occur), the operation fails, and the instruction
 * is not marked as successfully executed.
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute. The `arg0` field specifies the number of arguments
 *  to pass to the function.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Returns `true` if the function was successfully invoked and the result was pushed onto
 *  the stack, or `false` if the invocation failed (e.g., invalid function object, runtime error).
 */
static bool exec_CALL(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    object_t *func = pop_object_from_stack(thread->data_stack);
    bool result = func->vtbl->call(func, instr.arg0, thread);
    // The ID of the following instruction was set inside the call method
    DECREF(func);
    return result;
}

/**
 * @brief Executes the `RET` instruction.
 * 
 * The `RET` opcode terminates execution of the current function and returns control
 * to the caller.
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute (unused parameter).
 * @param thread Pointer to the executing thread.
 * @return Returns `true` if the return was successfully processed,
 *         or `false` if context restoration failed (should never occur in valid programs).
 */
static bool exec_RET(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    return false;
}

/**
 * @brief Executes the `ENTER` instruction.
 * 
 * The `ENTER` opcode creates a new execution context using the current context as a prototype.
 * This establishes a new variable scope while maintaining access to variables
 * from the parent scope.
 * 
 * Upon execution, this function:
 * - Creates a new context using the current thread's context as prototype
 * - Sets the thread's current context to the newly created one
 * - All subsequent variable operations will refer to this new context
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Always returns `true` as context creation cannot fail in current implementation.
 *         The instruction pointer is always advanced.
 */
static bool exec_ENTER(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    thread->context = create_context(thread->process, thread->context, NULL);
    thread->instr_id++;
    return true;
}

/**
 * @brief Executes the `LEAVE` instruction.
 * 
 * The `LEAVE` opcode restores the previous execution context while preserving the current one.
 * 
 * Upon execution, this function:
 * - Captures the current context's data object and pushes it onto the data stack
 * - Destroys the current context and restores the parent context
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Always returns `true` as context restoration cannot fail in current implementation.
 *         The instruction pointer is always advanced.
 */
static bool exec_LEAVE(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    context_t *context = thread->context;
    object_t *object = context->data;
    INCREF(object);
    thread->context = destroy_context(context);
    push_object_onto_stack(thread->data_stack, object);
    thread->instr_id++;
    return true;
}

/**
 * @brief Array of instruction execution functions for the Goat virtual machine.
 * 
 * This array stores function pointers corresponding to each available opcode in the
 * virtual machine. Each function in the array handles the execution of a single instruction,
 * based on the opcode provided.
 * 
 * The array is indexed by the opcode value, and the corresponding function is called to execute
 * the instruction for that opcode.
 */
static instr_executor_t executors[] = {
    exec_NOP,     /**< No operation - does nothing. */
    exec_ARG,     /**< Argument push onto the argument stack. */
    exec_END,     /**< Ends the program immediately. */
    exec_POP,     /**< Pops an object off the data stack. */
    exec_NIL,     /**< Pushes a null object onto the data stack. */
    exec_ILOAD32, /**< Pushes a 32-bit integer onto the data stack. */
    exec_ILOAD64, /**< Pushes a 64-bit integer onto the data stack. */
    exec_SLOAD,   /**< Pushes a static string onto the data stack. */
    exec_VLOAD,   /**< Loads a variable value onto the data stack or `null` if undefined. */
    exec_VAR,     /**< Declares a new mutable variable in current context. */
    exec_CONST,   /**< Declares a new immutable constant in current context. */
    exec_STORE,   /**< Stores to existing variable or creates new if not found. */
    exec_ADD,     /**< Adds the top two objects of the stack. */
    exec_SUB,     /**< Subtracts the top two objects of the stack. */
    exec_FUNC,    /**< Creates a new function object. */
    exec_CALL,    /**< Calls a function with arguments from the data stack. */
    exec_RET,     /**< Returns from current function. */
    exec_ENTER,   /**< Creates a new context, inheriting from the current one. */
    exec_LEAVE    /**< Restores the parent context, leaving the current one on the stack. */
    // Additional opcodes can be added here in the future...
};

int run(process_t *proc, bytecode_t *code) {

    // preparing the environment     
    runtime_t runtime;
    runtime.code = code;
    if ((proc->string_cache_size = code->data_descriptor_count) > 0) {
        proc->string_cache = CALLOC(code->data_descriptor_count * sizeof(object_t*));
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
    return 0;
}
