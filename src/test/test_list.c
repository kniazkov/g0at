/**
 * @file test_list.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Source file for managing and retrieving unit tests.
 */

#include "test_list.h"

#include "test_abstract_state.h"
#include "test_addition.h"
#include "test_analysis.h"
#include "test_codegen.h"
#include "test_division.h"
#include "test_lattice.h"
#include "test_lib.h"
#include "test_model.h"
#include "test_modulo.h"
#include "test_multiplication.h"
#include "test_operation_result.h"
#include "test_optimization.h"
#include "test_parser.h"
#include "test_power.h"
#include "test_reachability.h"
#include "test_scanner.h"
#include "test_subtraction.h"
#include "test_unary.h"

static bool stub() {
    return true;
}

static test_description_t test_list[] = {
    {"unary models and VM", test_unary_models_and_vm},
    {"unary errors", test_unary_errors},
    {"unary AST and domains", test_unary_ast_and_domains},
    {"unary parser", test_unary_parser},
    {"parsing function calls", test_parsing_function_calls},
    {"exception parser", test_exception_parser},
    {"catch binding", test_catch_binding},
    {"unclosed bracket", test_unclosed_bracket},
    {"missing opening bracket", test_missing_opening_bracket},
    {"closing bracket does not match opening", test_closing_bracket_does_not_match_opening},
    {"parsing of brackets with one level of nesting", test_brackets_one_level_nesting},
    {"parsing of brackets with two levels of nesting", test_brackets_two_levels_nesting},
    {"parsing static string", test_static_string},
    {"parsing identifier", test_identifier},
    {"parsing bracket", test_bracket},
    {"unknown symbol", test_uknown_symbol}

    ,
    {"optimization options", test_optimization_options},
    {"optimization modes", test_optimization_modes},
    {"optimized if bytecode", test_optimized_if_bytecode},
    {"reachability flags and events", test_reachability_flags},
    {"reachability bytecode", test_reachability_bytecode},
    {"reachability bytecode boundaries", test_reachability_bytecode_boundaries},
    {"reachability graph", test_reachability_graph},
    {"memory allocation", test_memory_allocation},
    {"allocation alignment", test_allocation_alignment},
    {"arena alignment and growth", test_arena_alignment_and_growth},
    {"binary search boundaries", test_binary_search_boundaries},
    {"path lifetime", test_path_lifetime},
    {"UTF-8 formatted output", test_utf8_formatted_output},
    {"UTF-8 file I/O", test_utf8_file_io},
    {"AVL tree", test_avl_tree},
    {"string builder", test_string_builder},
    {"string geometric growth", test_string_geometric_growth},
    {"UTF-8 roundtrip", test_utf8_roundtrip},
    {"UTF-8 invalid sequences", test_utf8_invalid_sequences},
    {"binary search", test_binary_search},
    {"double to string", test_double_to_string},
    {"format string", test_format_string},
    {"text alignment", test_align_text}

    ,
    {"vm throw cleanup", test_vm_throw_cleanup},
    {"vm restore", test_vm_restore},
    {"vm throw values", test_vm_throw_values},
    {"vm throw nested", test_vm_throw_nested},
    {"vm throw calls", test_vm_throw_calls},
    {"vm throw uncaught", test_vm_throw_uncaught},
    {"vm exception invalid bytecode", test_vm_exception_invalid_bytecode},
    {"exceptions object", test_exceptions_object},
    {"boolean object", test_boolean_object},
    {"integer object", test_integer_object},
    {"addition models and constants", test_addition_models_and_constants},
    {"addition domains", test_addition_domains},
    {"mixed numeric arithmetic", test_mixed_arithmetic},
    {"power models and constants", test_power_models_and_constants},
    {"power independent expectations", test_power_expected},
    {"power domains", test_power_domains},
    {"power ranges", test_power_ranges},
    {"modulo models and constants", test_modulo_models_and_constants},
    {"modulo independent expectations", test_modulo_expected},
    {"modulo domains and ranges", test_modulo_ranges},
    {"division models and constants", test_division_models_and_constants},
    {"division domains", test_division_domains},
    {"division ranges", test_division_ranges},
    {"division independent expectations", test_division_expected},
    {"multiplication models and constants", test_multiplication_models_and_constants},
    {"multiplication domains", test_multiplication_domains},
    {"multiplication ranges", test_multiplication_ranges},
    {"subtraction models and constants", test_subtraction_models_and_constants},
    {"subtraction domains", test_subtraction_domains},
    {"subtraction ranges", test_subtraction_ranges},
    {"addition ranges", test_addition_ranges},
    {"addition VM errors", test_addition_vm_errors},
    {"operation results", test_operation_results},
    {"operation VM dispatch", test_operation_vm_dispatch},
    {"operation result ownership", test_operation_result_ownership},
    {"addition of two integers", test_addition_of_two_integers},
    {"subtraction of two integers", test_subtraction_of_two_integers},
    {"string concatenation", test_strings_concatenation},
    {"properties", test_properties},
    {"string topology", test_string_topology},
    {"store and load", test_store_and_load},
    {"'sign' function", test_sign_function},
    {"context cloning", test_context_cloning},
    {"function definition", test_function_definition},
    {"closure", test_closure}

    ,
    {"unknown expression values", test_unknown_expression_values},
    {"unknown values in analysis", test_unknown_values_in_analysis},
    {"valueless nodes and known values", test_valueless_nodes_and_known_values},
    {"lattice examples", test_lattice_examples},
    {"lattice boundaries", test_lattice_boundaries},
    {"lattice arrays", test_lattice_arrays},
    {"lattice laws", test_lattice_laws},
    {"abstract state clone lifetimes", test_abstract_state_clone_lifetimes},
    {"abstract state many declarations", test_abstract_state_many_declarations},
    {"abstract state join isolation", test_abstract_state_join_isolation},
    {"abstract state clone metadata", test_abstract_state_clone_metadata},
    {"abstract state branch program", test_abstract_state_branch_program},
    {"abstract truthiness", test_abstract_truthiness},
    {"if dispatch", test_if_dispatch},
    {"if return values", test_if_return_values},
    {"analysis collector", test_analysis_collector},
    {"analysis collector text", test_analysis_collector_text},
    {"analysis observations", test_analysis_observations},
    {"analysis branch observations", test_analysis_branch_observations},
    {"analysis options", test_analysis_options}

    ,
    {"try catch structure", test_try_catch_structure},
    {"try catch bytecode", test_try_catch_bytecode},
    {"try catch analysis placeholder", test_try_catch_analysis_placeholder},
    {"data builder", test_data_builder},
    {"linker", test_linker},
    {"node codegen stubs", test_node_codegen_stubs},
    {"root object call", test_root_object_call}};

int get_number_of_tests() {
    return sizeof(test_list) / sizeof(test_description_t);
}

const test_description_t *get_tests() {
    return test_list;
}
