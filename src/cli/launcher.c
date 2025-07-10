/**
 * @file launcher.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Implementation of functions for launching the compiler and virtual machine.
 *
 * This file contains the implementation of functions responsible for starting the compiler and
 * virtual machine. These functions manage the initialization, execution, and cleanup of both
 * systems. They work with command-line options and interact with other components of the project.
 */

#include <stdio.h>

#include "launcher.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/io.h"
#include "resources/messages.h"
#include "scanner/scanner.h"
#include "parser/parser.h"
#include "codegen/linker.h"
#include "codegen/source_builder.h"
#include "graph/node.h"
#include "graph/visualization.h"
#include "vm/vm.h"

int go(options_t *opt) {
    /*
        1. setup
    */
    long previously_allocated = get_allocated_memory_size();
    if (opt->language) {
        set_language(opt->language);
    }

    /*
        2. read source file
    */
    string_value_t code = read_utf8_file(opt->input_file->full_path);
    if (code.data == NULL) {
        fprintf_utf8(stderr, get_messages()->cannot_read_source_file, opt->input_file->normal_path);
        return -1;
    }

    /*
        3. allocate memory for the parser
    */
    arena_t *tokens_memory = create_arena();
    arena_t *graph_memory = create_arena();
    parser_memory_t memory = { tokens_memory, graph_memory };
    token_groups_t *groups = (token_groups_t*)ALLOC(sizeof(token_groups_t));

    compilation_error_t *error = NULL;
    int ret_code = -1;
    
    do {
        /*
            4. scan (split code into tokens)
        */
        scanner_t *scan = create_scanner(opt->input_file->file_name, code.data, &memory, groups);
        token_list_t tokens;
        error = process_brackets(tokens_memory, scan, &tokens);
        if (error != NULL) {
            break;
        }

        /*
            5. build a syntax tree
        */
        error = apply_reduction_rules(groups, &memory);
        if (error != NULL) {
            break;
        }

        node_t *root_node;
        error = process_root_token_list(&memory, &tokens, &root_node);
        if (error != NULL) {
            break;
        }

        /*
            6. from now on we don't need tokens anymore, we can free this part of memory
        */
        FREE(groups);
        groups = NULL;
        destroy_arena(tokens_memory);
        tokens_memory = NULL;

        /*
            7. print source code (if needed)
        */
        if (opt->print_source_code) {
            source_builder_t *source_builder = create_source_builder();
            root_node->vtbl->generate_indented_goat_code(root_node, source_builder, 0);
            string_value_t source_code = build_source_code(source_builder);
            if (source_code.data) {
                print_utf8(source_code.data);
                if (source_code.should_free) {
                    FREE(source_code.data);
                }
            }
            destroy_source_builder(source_builder);
        }

        /*
            8. visualization (if needed)
        */
        if (opt->graph_output_file != NULL) {
            if (is_graphviz_available()) {
                bool image_generated = generate_image(root_node, opt->graph_output_file->full_path);
                if (!image_generated) {
                    fprintf_utf8(stderr, get_messages()->graphviz_failed);
                    fprintf(stderr, "\n");
                }
            } else {
                fprintf_utf8(stderr, get_messages()->no_graphviz);
                fprintf(stderr, "\n");
            }
        }

        /*
            9. compile the syntax tree into bytecode
        */
        code_builder_t *code_builder = create_code_builder();
        data_builder_t *data_builder = create_data_builder();
        root_node->vtbl->generate_bytecode(root_node, code_builder, data_builder);
        bytecode_t *bytecode = link_code_and_data(code_builder, data_builder);
        destroy_code_builder(code_builder);
        destroy_data_builder(data_builder);

        /*
            10. print bytecode (if needed)
        */
        if (opt->print_bytecode) {
            string_value_t text = bytecode_to_text(bytecode);
            print_utf8(text.data);
            if (text.should_free) {
                FREE(text.data);
            }
        }
        
        /*
            11. destroy the syntax tree, since the bytecode exists
        */
        destroy_arena(graph_memory);
        graph_memory = NULL;
        if (code.should_free) {
            FREE(code.data);
            code.data = NULL;
        }

        /*
            12. run the virtual machine
        */
        process_t *process = create_process();
        ret_code = run(process, bytecode);
        destroy_process(process);

        /*
            13. destroy bytecode
        */
        free_bytecode(bytecode);
    } while(false);

    /*
        14. print error messages (if any)
    */
    if (error != NULL) {
        const wchar_t const *error_msg_format = get_messages()->compilation_error;
        while (error != NULL) {
            fprintf_utf8(stderr, error_msg_format, error->begin.file_name, error->begin.row,
                error->begin.column, error->message.data);
            fprintf(stderr, "\n");
            error = error->next;
        }
    }

    /*
        15. free the memory used by the compiler if it is not free yet
    */
    if (graph_memory != NULL) {
        destroy_arena(graph_memory);
    }
    FREE(groups);
    if (tokens_memory != NULL) {
        destroy_arena(tokens_memory);
    }
    if (code.data != NULL && code.should_free) {
        FREE(code.data);
    }

    /*
        16. check for memory leaks
    */
    size_t leaked_memory_size = get_allocated_memory_size() - previously_allocated;
    if (leaked_memory_size > 0) {
        fprintf(stderr, "\n");
        fprintf_utf8(stderr, get_messages()->memory_leak, leaked_memory_size);
        fprintf(stderr, "\n");
        return -1;
    }

    return ret_code;
}
