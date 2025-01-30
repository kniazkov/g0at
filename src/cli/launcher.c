/**
 * @file launcher.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of functions for launching the compiler and virtual machine.
 *
 * This file contains the implementation of functions responsible for starting the compiler and
 * virtual machine. These functions manage the initialization, execution, and cleanup of both
 * systems. They work with command-line options and interact with other components of the project.
 */

#include "launcher.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/io.h"
#include "scanner/scanner.h"
#include "parser/parser.h"
#include "codegen/linker.h"
#include "graph/node.h"
#include "vm/vm.h"

int go(options_t *opt) {
    /*
        1. read source file
    */
    string_value_t code = read_utf8_file(opt->input_file);
    if (code.data == NULL) {
        return -1;
    }

    /*
        2. allocate memory for the parser
    */
    arena_t *tokens_memory = create_arena();
    arena_t *graph_memory = create_arena();
    parser_memory_t memory = { tokens_memory, graph_memory };

    compilation_error_t *error = NULL;
    int ret_code = -1;
    
    do {
        /*
            3. scan (split code into tokens)
        */
        token_groups_t groups;
        scanner_t *scan = create_scanner(opt->input_file, code.data, &memory, &groups);
        token_list_t tokens;
        error = process_brackets(tokens_memory, scan, &tokens);
        if (error != NULL) {
            break;
        }

        /*
            4. build a syntax tree
        */
        error = apply_reduction_rules(&groups, &memory);
        if (error != NULL) {
            break;
        }

        root_token_list_processing_result_t rtlpr = process_root_token_list(&memory, &tokens);
        if (rtlpr.error != NULL) {
            error = rtlpr.error;
            break;
        }

        /*
            5. from now on we don't need tokens anymore, we can free this part of memory
        */
        destroy_arena(tokens_memory);
        tokens_memory = NULL;

        /*
            6. compile the syntax tree into bytecode
        */
        code_builder_t *code_builder = create_code_builder();
        data_builder_t *data_builder = create_data_builder();
        rtlpr.root_node->vtbl->gen_bytecode(rtlpr.root_node, code_builder, data_builder);
        bytecode_t *bytecode = link_code_and_data(code_builder, data_builder);
        destroy_code_builder(code_builder);
        destroy_data_builder(data_builder);
        
        /*
            7. destroy the syntax tree, since the bytecode exists
        */
        destroy_arena(graph_memory);
        graph_memory = NULL;
        if (code.should_free) {
            FREE(code.data);
            code.data = NULL;
        }

        /*
            8. run the virtual machine
        */
        process_t *process = create_process();
        ret_code = run(process, bytecode);
        destroy_process(process);

        /*
            9. destroy bytecode
        */
        free_bytecode(bytecode);
    } while(false);

    /*
        10. free the memory used by the compiler if it is not free yet
    */
    if (graph_memory != NULL) {
        destroy_arena(graph_memory);
    }
    if (tokens_memory != NULL) {
        destroy_arena(tokens_memory);
    }
    if (code.data != NULL && code.should_free) {
        FREE(code.data);
    }

    if (get_allocated_memory_size() > 0) {
        return -1;
    }

    return ret_code;
}
