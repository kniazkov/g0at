/** @file test_stack_underflow.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Verifies that stack underflow exits even inside TRY.
 */
#include "test_stack_underflow.h"

#include "codegen/linker.h"
#include "model/context.h"
#include "model/process.h"
#include "model/thread.h"
#include "vm/vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#    include <sys/wait.h>
#endif

static const struct {
    opcode_t opcode;
    int operands;
} cases[] = {
    {LNOT, 0},   {BOOL, 0},   {BNOT, 0},   {LAND, 0},    {LOR, 0},     {BAND, 0},  {BAND, 1},
    {BOR, 0},    {BOR, 1},    {BXOR, 0},   {BXOR, 1},    {SHL, 0},     {SHL, 1},   {SHR, 0},
    {SHR, 1},    {FUNC, 0},   {JIF, 0},    {POP, 0},     {DUP, 0},     {VAR, 0},   {CONST, 0},
    {STORE, 0},  {UPLUS, 0},  {UMINUS, 0}, {INC, 0},     {DEC, 0},     {THROW, 0}, {RET, 0},
    {CALL, 0},   {ADD, 0},    {ADD, 1},    {SUB, 0},     {SUB, 1},     {MUL, 0},   {MUL, 1},
    {DIVIDE, 0}, {DIVIDE, 1}, {MODULO, 0}, {MODULO, 1},  {POWER, 0},   {POWER, 1}, {LESS, 0},
    {LESS, 1},   {LEQ, 0},    {LEQ, 1},    {GREATER, 0}, {GREATER, 1}, {GREQ, 0},  {GREQ, 1},
    {EQUAL, 0},  {EQUAL, 1},  {DIFF, 0},   {DIFF, 1},    {CALL, 1},    {RET, 1}};

void run_stack_underflow_case(int index) {
    process_t *proc = create_process();
    object_stack_t *stack = proc->main_thread->data_stack;
    size_t count = sizeof(cases) / sizeof(*cases);
    if ((size_t)index >= count) {
        switch ((size_t)index - count) {
            case 0:
                pop_object_from_stack(stack);
                break;
            case 1:
                peek_object_from_stack(stack, 0);
                break;
            case 2:
                push_object_onto_stack(stack, get_null_object());
                peek_object_from_stack(stack, 1);
                break;
            case 3:
                replace_object_on_stack(stack, get_null_object(), 0);
                break;
            case 4:
                reduce_object_stack(stack, 0);
                break;
            case 5:
                call_object(get_function_print(), 1, proc->main_thread);
                break;
            case 6: {
                object_t *func =
                    create_function_object(proc, NULL, 0, 0, proc->main_thread->context->data);
                call_object(func, 1, proc->main_thread);
                break;
            }
            case 7: {
                code_builder_t *b = create_code_builder();
                data_builder_t *d = create_data_builder();
                instruction_t code[] = {{.opcode = ILOAD32},
                                        {.opcode = TRY, .arg1 = 5},
                                        {.opcode = POP},
                                        {.opcode = NIL},
                                        {.opcode = THROW},
                                        {.opcode = END}};
                for (size_t i = 0; i < 6; i++)
                    add_instruction(b, code[i]);
                run(proc, link_code_and_data(b, d));
                break;
            }
        }
        return;
    }
    for (int i = 0; i < cases[index].operands; i++)
        push_object_onto_stack(stack, get_static_integer_object(7));
    proc->main_thread->context->ret_value_index = 0;
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    uint32_t key = add_string_to_data_segment(data, L"x");
    add_instruction(builder, (instruction_t){.opcode = TRY, .arg1 = 2});
    add_instruction(builder,
                    (instruction_t){.opcode = cases[index].opcode,
                                    .arg0 = cases[index].opcode == CALL ? 2 : 0,
                                    .arg1 = key});
    add_instruction(builder, (instruction_t){.opcode = END});
    run(proc, link_code_and_data(builder, data));
}

bool test_stack_underflow(const char *executable) {
    const char *file = "stack-underflow-test-error.txt";
    size_t count = sizeof(cases) / sizeof(*cases) + 8;
    for (size_t i = 0; i < count; i++) {
        size_t capacity = strlen(executable) + 128;
        char *command = malloc(capacity);
#ifdef _WIN32
        snprintf(command,
                 capacity,
                 "\"\"%s\" --stack-underflow-case %zu 2> %s\"",
                 executable,
                 i,
                 file);
#else
        snprintf(command, capacity, "\"%s\" --stack-underflow-case %zu 2> %s", executable, i, file);
#endif
        int status = system(command);
        free(command);
#ifdef _WIN32
        bool exited = status == EXIT_FAILURE;
#else
        bool exited = status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == EXIT_FAILURE;
#endif
        FILE *stream = fopen(file, "r");
        char message[256] = {0};
        size_t length = stream ? fread(message, 1, sizeof(message) - 1, stream) : 0;
        if (stream)
            fclose(stream);
        remove(file);
        if (!exited || !length
            || strcmp(message, "FATAL: Stack underflow! The interpreter is broken. Aborting.\n")) {
            fprintf(stderr,
                    "Stack underflow case %zu failed: status=%d, stderr=%s\n",
                    i,
                    status,
                    message);
            return false;
        }
    }
    printf("Fatal stack testing: %zu/%zu passed\n", count, count);
    return true;
}
