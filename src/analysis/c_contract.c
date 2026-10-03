/** @file c_contract.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Numeric interface checks, independent of future C body proofs.
 */
#include "c_contract.h"

#include "function_call_graph.h"
#include "graph/expression.h"
#include "graph/variable.h"
#include "lib/string_ext.h"

c_value_type_t classify_c_value_type(lattice_type_t type) {
    if (is_integer_lattice_type(type))
        return C_VALUE_INT64;
    if (is_real_lattice_type(type))
        return C_VALUE_DOUBLE;
    if (type == LATTICE_TOP || type == LATTICE_NOT_NULL || type == LATTICE_NUMERIC
        || type == LATTICE_BOTTOM)
        return C_VALUE_UNKNOWN;
    return C_VALUE_UNSUPPORTED;
}

/** @brief A static function identity needs no captured data slot. */
static bool static_function_capture(const function_capture_t *capture) {
    if (capture->access != FUNCTION_CAPTURE_READ || !capture->declarator
        || capture->declarator == get_builtin_declarator()
        || capture->declarator->base.vtbl->type != NODE_CONSTANT_DECLARATOR)
        return false;
    return resolve_immutable_function(get_node_child(&capture->declarator->base, 0)) != NULL;
}

void check_function_c_contract(function_summary_t *summary) {
    uint32_t blockers = C_BLOCKER_BODY;
    bool unsupported = false;
    for (size_t i = 0; i < summary->parameter_count; i++) {
        c_value_type_t type = classify_c_value_type(summary->parameter_types[i]->type);
        if (type == C_VALUE_UNKNOWN || type == C_VALUE_UNSUPPORTED)
            blockers |= C_BLOCKER_PARAMETERS;
        unsupported |= type == C_VALUE_UNSUPPORTED;
    }
    c_value_type_t result = classify_c_value_type(summary->return_type->type);
    if (result == C_VALUE_UNKNOWN || result == C_VALUE_UNSUPPORTED)
        blockers |= C_BLOCKER_RETURN;
    unsupported |= result == C_VALUE_UNSUPPORTED;
    if (!function_summary_is_pure(summary))
        blockers |= C_BLOCKER_EFFECTS;
    for (const function_capture_t *capture = summary->captures; capture; capture = capture->next) {
        if (!static_function_capture(capture)) {
            blockers |= C_BLOCKER_CAPTURES;
            unsupported = true;
        }
    }
    /* Default direct facts do not prove absence of captures. */
    if (summary->direct_effects & FUNCTION_EFFECT_UNKNOWN)
        blockers |= C_BLOCKER_ANALYSIS;
    summary->c_blockers = blockers;
    summary->c_support = unsupported ? FUNCTION_C_UNSUPPORTED : FUNCTION_C_UNKNOWN;
}

void analyze_function_c_contracts(node_t *root) {
    if (root->vtbl->type == NODE_FUNCTION_OBJECT) {
        for (function_summary_t *summary = get_function_summaries(root)->head; summary;
             summary = summary->next)
            check_function_c_contract(summary);
    }
    for (size_t i = 0; i < get_node_child_count(root); i++)
        analyze_function_c_contracts(get_node_child(root, i));
}

string_value_t c_blockers_to_string(uint32_t blockers) {
    if (!blockers)
        return STATIC_STRING(L"none");
    const uint32_t bits[] = {C_BLOCKER_ANALYSIS,
                             C_BLOCKER_PARAMETERS,
                             C_BLOCKER_RETURN,
                             C_BLOCKER_EFFECTS,
                             C_BLOCKER_CAPTURES,
                             C_BLOCKER_BODY};
    const wchar_t *names[] =
        {L"analysis", L"parameters", L"return", L"effects", L"captures", L"body"};
    string_builder_t builder;
    init_string_builder(&builder, 0);
    bool separator = false;
    string_value_t result = EMPTY_STRING_VALUE;
    for (size_t i = 0; i < sizeof(bits) / sizeof(*bits); i++) {
        if (blockers & bits[i]) {
            if (separator)
                append_char(&builder, L'|');
            result = append_string(&builder, names[i]);
            separator = true;
        }
    }
    return result;
}
