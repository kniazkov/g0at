/**
 * @file vm.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Goat virtual machine.
 */

#include <stdbool.h>

#include "vm.h"
#include "gc.h"
#include "model/thread.h"
#include "lib/split64.h"

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
 * @param bytecode The bytecode that contains the instructions and static data.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return A boolean value indicating whether the virtual machine should continue 
 *  executing the next instruction (`true`), or halt (`false`).
 */
typedef bool (*instr_executor_t)(bytecode_t *code, instruction_t instr, thread_t *thread);


/**
 * @brief Executes the NOP instruction.
 * 
 * The `NOP` opcode performs no operation and simply advances the instruction pointer.
 * It is typically used as a placeholder or for debugging.
 * 
 * @param bytecode The bytecode that contains the instructions and static data.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Always returns `true` to continue executing the next instruction.
 */
static bool exec_NOP(bytecode_t *code, instruction_t instr, thread_t *thread) {
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
 * @param bytecode The bytecode that contains the instructions and static data.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return `true` if the argument was successfully pushed, `false` if the stack is full.
 */
static bool exec_ARG(bytecode_t *code, instruction_t instr, thread_t *thread) {
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
 * @param bytecode The bytecode that contains the instructions and static data.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Always returns `false` to terminate the execution.
 */
static bool exec_END(bytecode_t *code, instruction_t instr, thread_t *thread) {
    return false;
}

/**
 * @brief Executes the POP instruction.
 * 
 * The `POP` opcode removes the topmost object from the data stack. It is used when an object
 * is no longer needed and should be discarded from the stack.
 * 
 * @param bytecode The bytecode that contains the instructions and static data.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Always returns `true` to continue executing the next instruction.
 */
static bool exec_POP(bytecode_t *code, instruction_t instr, thread_t *thread) {
    pop_object_from_stack(thread->data_stack);
    thread->instr_id++;
    return true;
}

/**
 * @brief Executes the ILOAD32 instruction.
 * 
 * The `ILOAD32` opcode pushes a 32-bit integer onto the data stack. This operation loads a
 * 32-bit integer value from the instruction argument and pushes it onto the stack.
 * 
 * @param bytecode The bytecode that contains the instructions and static data.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Always returns `true` to continue executing the next instruction.
 */
static bool exec_ILOAD32(bytecode_t *code, instruction_t instr, thread_t *thread) {
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
 * @param bytecode The bytecode that contains the instructions and static data.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return `true` if the value was successfully pushed, `false` if the argument stack is not
 *  properly set up.
 */
static bool exec_ILOAD64(bytecode_t *code, instruction_t instr, thread_t *thread) {
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
 * @brief Executes the `ADD` instruction.
 * 
 * The `ADD` opcode performs an addition operation on the top two objects on the data stack.
 * It pops the top two objects from the stack, adds them using their respective `add` methods,
 * and pushes the result back onto the stack. If either of the objects cannot be added, the
 * operation fails.
 * 
 * @param code The bytecode containing the instructions and static data.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Returns `true` if the addition was successful and the result was pushed onto the stack,
 *  or `false` if the addition failed (e.g., due to invalid object types).
 */
static bool exec_ADD(bytecode_t *code, instruction_t instr, thread_t *thread) {
    object_t *first = pop_object_from_stack(thread->data_stack);
    object_t *second = pop_object_from_stack(thread->data_stack);
    if (first && second) {
        object_t *result = first->vtbl->add(first, second);
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
 * @param code The bytecode containing the instructions and static data.
 * @param instr The instruction to execute.
 * @param thread Pointer to the thread that is executing the instruction.
 * @return Returns `true` if the subtraction was successful and the result was pushed onto the
 *  stack, or `false` if the subtraction failed (e.g., due to invalid object types).
 */
static bool exec_SUB(bytecode_t *code, instruction_t instr, thread_t *thread) {
    object_t *first = pop_object_from_stack(thread->data_stack);
    object_t *second = pop_object_from_stack(thread->data_stack);
    if (first && second) {
        object_t *result = first->vtbl->sub(first, second);
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
    exec_ADD,     /**< Adds the top two objects of the stack. */
    exec_SUB,     /**< Subtracts the top two objects of the stack. */
    // Additional opcodes can be added here in the future...
};

int run(process_t *main_proc, bytecode_t *code) {
    bool flag = true;
    thread_t *thread = main_proc->main_thread;
    while (flag) {
        instruction_t instr = code->instructions[thread->instr_id];
        flag =  executors[instr.opcode](code, instr, thread);
        thread = thread->next;
    }
    collect_garbage(main_proc);
    return 0;
}
