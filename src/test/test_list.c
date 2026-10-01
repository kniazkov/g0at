/**
 * @file test_list.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Source file for managing and retrieving unit tests.
 */

#include "test_list.h"
#include "test_lib.h"
#include "test_scanner.h"
#include "test_parser.h"
#include "test_model.h"
#include "test_codegen.h"
#include "test_analysis.h"
#include "test_abstract_state.h"
#include "test_lattice.h"

static bool stub() {
    return true;
}

static test_description_t test_list[] = {
      { "parsing function calls", test_parsing_function_calls }
    , { "unclosed bracket", test_unclosed_bracket }
    , { "missing opening bracket", test_missing_opening_bracket }
    , { "closing bracket does not match opening", test_closing_bracket_does_not_match_opening }
    , { "parsing of brackets with one level of nesting", test_brackets_one_level_nesting }
    , { "parsing of brackets with two levels of nesting", test_brackets_two_levels_nesting }
    , { "parsing static string", test_static_string }
    , { "parsing identifier", test_identifier }
    , { "parsing bracket", test_bracket }
    , { "unknown symbol", test_uknown_symbol }

    , { "memory allocation", test_memory_allocation }
    , { "allocation alignment", test_allocation_alignment }
    , { "arena alignment and growth", test_arena_alignment_and_growth }
    , { "binary search boundaries", test_binary_search_boundaries }
    , { "path lifetime", test_path_lifetime }
    , { "UTF-8 formatted output", test_utf8_formatted_output }
    , { "UTF-8 file I/O", test_utf8_file_io }
    , { "AVL tree", test_avl_tree }
    , { "string builder", test_string_builder }
    , { "string geometric growth", test_string_geometric_growth }
    , { "UTF-8 roundtrip", test_utf8_roundtrip }
    , { "UTF-8 invalid sequences", test_utf8_invalid_sequences }
    , { "binary search", test_binary_search }
    , { "double to string", test_double_to_string }
    , { "format string", test_format_string }
    , { "text alignment", test_align_text }

    , { "boolean object", test_boolean_object }
    , { "integer object", test_integer_object }
    , { "addition of two integers", test_addition_of_two_integers }
    , { "subtraction of two integers", test_subtraction_of_two_integers }
    , { "string concatenation", test_strings_concatenation }
    , { "properties", test_properties }
    , { "string topology", test_string_topology }
    , { "store and load", test_store_and_load }
    , { "'sign' function", test_sign_function }
    , { "context cloning", test_context_cloning }
    , { "function definition", test_function_definition }
    , { "closure", test_closure }

    , { "unknown expression values", test_unknown_expression_values }
    , { "unknown values in analysis", test_unknown_values_in_analysis }
    , { "valueless nodes and known values", test_valueless_nodes_and_known_values }
    , { "lattice examples", test_lattice_examples }
    , { "lattice boundaries", test_lattice_boundaries }
    , { "lattice arrays", test_lattice_arrays }
    , { "lattice laws", test_lattice_laws }
    , { "abstract state clone lifetimes", test_abstract_state_clone_lifetimes }
    , { "abstract state many declarations", test_abstract_state_many_declarations }
    , { "abstract state join isolation", test_abstract_state_join_isolation }
    , { "abstract state clone metadata", test_abstract_state_clone_metadata }
    , { "abstract state branch program", test_abstract_state_branch_program }
    , { "analysis collector", test_analysis_collector }
    , { "analysis collector text", test_analysis_collector_text }
    , { "analysis observations", test_analysis_observations }
    , { "analysis branch observations", test_analysis_branch_observations }
    , { "analysis options", test_analysis_options }

    , { "data builder", test_data_builder }
    , { "linker", test_linker }
    , { "node codegen stubs", test_node_codegen_stubs }
    , { "root object call", test_root_object_call }
};

int get_number_of_tests() {
    return sizeof(test_list) / sizeof(test_description_t);
}

const test_description_t *get_tests() {
    return test_list;
}
