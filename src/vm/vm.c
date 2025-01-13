/**
 * @file vm.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Goat virtual machine.
 */

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
 * within the virtual machine. It includes the bytecode, static data objects, and other 
 * runtime-related information necessary for the program's execution.
 */
typedef struct {
    /**
     * @brief Pointer to the bytecode being executed.
     */
    bytecode_t *code;

    /**
     * @brief Cache of static objects.
     * @note Static data objects are not managed by the garbage collector and should 
     * be manually cleaned up when the process ends.
     */
    object_t **static_data;
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
        size_t i = 0;
        do {
            value = proto.items[i]->vtbl->get_property(proto.items[i], key);
            i++;
        } while (value == NULL);
    }
    if (value == NULL) {
        value = get_null_object();
    }
    return value;
}

/**
 * @brief Loads a static string from the bytecode or retrieves it from the cache.
 * 
 * This function either retrieves a static string from the cache (if it has already been loaded),
 * or loads it from the bytecode using its `string_id`. Static strings are predefined in the 
 * bytecode, and they are stored in a cache for efficient reuse. The string data is located 
 * in the bytecode at the position specified by the `data_descriptors`, and it is lazily loaded 
 * the first time it is accessed.
 * 
 * @param runtime The runtime environment containing the static data cache and bytecode.
 * @param string_id The identifier of the static string to load.
 * @return A pointer to the `object_t` representing the static string.
 */
static object_t *load_static_string(runtime_t *runtime, uint32_t string_id) {
    object_t *string = runtime->static_data[string_id];
    if (string == NULL) {
        data_descriptor_t descriptor = runtime->code->data_descriptors[string_id];
        string = create_static_string_object(
            (wchar_t*)(runtime->code->data + descriptor.offset),
            descriptor.size / sizeof(wchar_t) - 1
        );
        runtime->static_data[string_id] = string;
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
    object_t *string = load_static_string(runtime, string_id);
    push_object_onto_stack(thread->data_stack, string);
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
    object_t *key = load_static_string(runtime, string_id);
    object_t *value = get_property_from_object_or_its_prototypes(thread->context->data, key);
    push_object_onto_stack(thread->data_stack, value);
    thread->instr_id++;
    return true;
}

/**
 * @brief Executes the STORE opcode to store a value in the current context.
 * 
 * The `STORE` opcode stores a value from the stack into the current context's data object,
 * associating it with the property identified by `string_id`. The value is popped from the stack,
 * and the property is set in the context's data object. If the stack is empty or the property
 * cannot be set, the operation will fail.
 * 
 * @param runtime The runtime environment.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return `true` if the value was successfully stored in the context, `false` if there
 *  is an error (e.g., empty stack or bytecode corruption).
 */
static bool exec_STORE(runtime_t *runtime, instruction_t instr, thread_t *thread) {
    uint32_t string_id = instr.arg1;
    if (string_id >= runtime->code->data_descriptor_count) {
        return false; // bad bytecode
    }
    object_t *key = load_static_string(runtime, string_id);
    object_t *value = pop_object_from_stack(thread->data_stack);
    if (value == NULL) {
        return false; // empty stack
    }
    thread->context->data->vtbl->set_property(thread->context->data, key, value);
    DECREF(value);
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
    object_t *first = pop_object_from_stack(thread->data_stack);
    object_t *second = pop_object_from_stack(thread->data_stack);
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
    object_t *first = pop_object_from_stack(thread->data_stack);
    object_t *second = pop_object_from_stack(thread->data_stack);
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
    exec_ILOAD32, /**< Pushes a 32-bit integer onto the data stack. */
    exec_ILOAD64, /**< Pushes a 64-bit integer onto the data stack. */
    exec_SLOAD,   /**< Pushes a static string onto the data stack. */
    exec_VLOAD,   /**< Loads a variable value onto the data stack or `null` if undefined. */
    exec_STORE,   /**< Stores a value from the data stack into the current context. */
    exec_ADD,     /**< Adds the top two objects of the stack. */
    exec_SUB,     /**< Subtracts the top two objects of the stack. */
    // Additional opcodes can be added here in the future...
};

int run(process_t *main_proc, bytecode_t *code) {
    bool flag = true;
    thread_t *thread = main_proc->main_thread;
    runtime_t runtime;
    runtime.code = code;
    if (code->data_descriptor_count > 0) {
        runtime.static_data = CALLOC(code->data_descriptor_count * sizeof(object_t*));
    } else {
        runtime.static_data = NULL;
    }
    while (flag) {
        instruction_t instr = code->instructions[thread->instr_id];
        flag =  executors[instr.opcode](&runtime, instr, thread);
        thread = thread->next;
    }
    collect_garbage(main_proc);
    for (size_t i = 0; i < code->data_descriptor_count; i++) {
        FREE(runtime.static_data[i]);
    }
    FREE(runtime.static_data);
    return 0;
}
