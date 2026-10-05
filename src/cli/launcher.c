/**
 * @file launcher.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implementation of functions for launching the compiler and virtual machine.
 */

#include "launcher.h"

#include "analysis/analysis.h"
#include "binary_program.h"
#include "c_output.h"
#include "codegen/linker.h"
#include "codegen/source_builder.h"
#include "graph/node.h"
#include "graph/visualization.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/io.h"
#include "model/object.h"
#include "model/thread.h"
#include "native_execution.h"
#include "parser/parser.h"
#include "resources/messages.h"
#include "scanner/scanner.h"
#include "vm/vm.h"

#include <stdio.h>
#include <stdlib.h>

int go(options_t *opt) {
    long previously_allocated = get_allocated_memory_size();
    if (opt->language) {
        set_language(opt->language);
    }

    if (opt->run_binary) {
        int status = run_binary_program(opt);
        if (get_allocated_memory_size() != previously_allocated) {
            fprintf(stderr, "Memory leak after compiled execution.\n");
            return -1;
        }
        return status;
    }

    string_value_t code = read_utf8_file(opt->input_file->full_path);
    if (code.data == NULL) {
        fprintf_utf8(stderr, get_messages()->cannot_read_source_file, opt->input_file->normal_path);
        return -1;
    }

    parser_memory_t memory = {
        create_arena(64),  // positions
        create_arena(64),  // tokens
        create_arena(128), // nodes
        create_arena(8)    // errors
    };
    token_groups_t *groups = (token_groups_t *)ALLOC(sizeof(token_groups_t));

    compilation_error_t *error = NULL;
    int ret_code = -1;

    do {
        scanner_t *scan = create_scanner(opt->input_file->file_name, code, &memory, groups);
        token_list_t tokens;
        error = process_brackets(&memory, scan, &tokens, groups);
        if (error != NULL) {
            break;
        }

        parsing_result_t parsing_result = {0};
        error = apply_reduction_rules(groups, &memory, &parsing_result);
        if (error != NULL) {
            break;
        }

        node_t *root_node;
        error = process_root_token_list(&memory, &tokens, &root_node);
        if (error != NULL) {
            break;
        }

        FREE(groups);
        groups = NULL;
        destroy_arena(memory.tokens);
        memory.tokens = NULL;

        analysis_collector_t *collector = (opt->print_analysis || opt->analysis_output_file)
                                              ? create_analysis_collector(memory.graph)
                                              : NULL;
        error = analyze(root_node, &memory, opt, collector);
        compilation_error_severity_t severity = get_most_severe_compilation_error(error);
        if (severity > WARNING) {
            break;
        }

        if (error != NULL) {
            error = reverse_compilation_errors(error);
            const wchar_t const *error_msg_format = get_messages()->compilation_warning;
            while (error != NULL) {
                fprintf_utf8(stderr,
                             error_msg_format,
                             error->position->begin->file_name,
                             error->position->begin->row,
                             error->position->begin->column,
                             error->message.data);
                fprintf(stderr, "\n");
                error = error->next;
            }
        }
        destroy_arena(memory.errors);
        memory.errors = NULL;
        error = NULL;

        if (collector) {
            string_value_t report = analysis_collector_to_text(collector);
            if (opt->print_analysis) {
                print_utf8(report.data);
            }
            bool written = !opt->analysis_output_file
                           || write_utf8_file(opt->analysis_output_file->full_path, report.data);
            FREE_STRING(report);
            if (!written) {
                fprintf_utf8(stderr,
                             get_messages()->cannot_write_analysis_file,
                             opt->analysis_output_file->normal_path);
                fprintf(stderr, "\n");
                break;
            }
        }

        if (opt->print_source_code) {
            source_builder_t *source_builder = create_source_builder();
            generate_indented_goat_code_from_node(root_node, source_builder, 0);
            string_value_t source_code = build_source(source_builder);
            if (source_code.data) {
                print_utf8(source_code.data);
                FREE_STRING(source_code);
            }
            destroy_source_builder(source_builder);
        }

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

        if (opt->print_c || opt->save_c || opt->save_library) {
            ret_code = output_c_module(opt, memory.graph, root_node) ? 0 : -1;
            break;
        }

        code_builder_t *code_builder = create_code_builder();
        data_builder_t *data_builder = create_data_builder();
        generate_bytecode_from_node(root_node, code_builder, data_builder);
        bool processed_all, progressed;
        do {
            processed_all = true;
            progressed = false;
            list_item_t *func_item = parsing_result.functions->head;
            while (func_item) {
                list_item_t *next_item = func_item->next;
                node_t *func_obj = (node_t *)func_item->value.ptr;
                if (generate_deferred_bytecode_from_node(func_obj, code_builder, data_builder)) {
                    remove_item_from_linked_list(parsing_result.functions, func_item);
                    progressed = true;
                } else {
                    processed_all = false;
                }
                func_item = next_item;
            }
        } while (!processed_all && progressed);
        bytecode_t *bytecode = link_code_and_data(code_builder, data_builder);
        destroy_code_builder(code_builder);
        destroy_data_builder(data_builder);

        if (opt->compile_only) {
            if (opt->print_bytecode) {
                string_value_t text = bytecode_to_text(bytecode);
                print_utf8(text.data);
                FREE_STRING(text);
            }
            ret_code = compile_binary_program(opt, root_node, bytecode) ? 0 : -1;
            free_bytecode(bytecode);
            break;
        }

        native_execution_report_t native;
        if (!prepare_native_program(opt, root_node, bytecode, &native)) {
            output_native_report(opt, &native, NULL);
            free_bytecode(bytecode);
            break;
        }

        destroy_arena(memory.graph);
        memory.graph = NULL;
        FREE_STRING(code);
        code = NULL_STRING_VALUE;

        ret_code = execute_program(opt, bytecode, &native);

        free_bytecode(bytecode);
    } while (false);

    if (error != NULL) {
        error = reverse_compilation_errors(error);
        while (error != NULL) {
            const wchar_t const *error_msg_format = NULL;
            switch (error->severity) {
                case WARNING:
                    error_msg_format = get_messages()->compilation_warning;
                    break;
                case ERROR:
                    error_msg_format = get_messages()->compilation_error;
                    break;
                case CRITICAL:
                    error_msg_format = get_messages()->critical_compilation_error;
                    break;
            }
            fprintf_utf8(stderr,
                         error_msg_format,
                         error->position->begin->file_name,
                         error->position->begin->row,
                         error->position->begin->column,
                         error->message.data);
            fprintf(stderr, "\n");
            error = error->next;
        }
    }

    if (memory.positions != NULL) {
        destroy_arena(memory.positions);
    }
    if (memory.tokens != NULL) {
        destroy_arena(memory.tokens);
    }
    FREE(groups);
    if (memory.graph != NULL) {
        destroy_arena(memory.graph);
    }
    FREE_STRING(code);
    if (memory.errors != NULL) {
        destroy_arena(memory.errors);
    }

    size_t leaked_memory_size = get_allocated_memory_size() - previously_allocated;
    if (leaked_memory_size > 0) {
        fprintf(stderr, "\n");
        fprintf_utf8(stderr, get_messages()->memory_leak, leaked_memory_size);
        fprintf(stderr, "\n");
        return -1;
    }

    return ret_code;
}
