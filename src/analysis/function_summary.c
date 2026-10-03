/** @file function_summary.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Function summary lifetime and diagnostic rendering.
 */
#include "function_summary.h"

#include "lib/allocate.h"
#include "lib/string_ext.h"

#include <assert.h>
#include <string.h>

function_summary_t *
create_function_summary(arena_t *arena, const node_t *function, size_t parameter_count) {
    assert(arena && function);
    function_summary_t *summary = alloc_zeroed_from_arena(arena, sizeof(*summary));
    summary->function = function;
    summary->parameter_count = parameter_count;
    if (parameter_count)
        summary->parameter_types =
            alloc_from_arena(arena, parameter_count * sizeof(*summary->parameter_types));
    for (size_t i = 0; i < parameter_count; i++)
        summary->parameter_types[i] = make_top_element();
    reset_function_summary(summary);
    return summary;
}

void reset_function_summary(function_summary_t *summary) {
    summary->return_type = make_top_element();
    summary->effects = FUNCTION_EFFECT_UNKNOWN;
    summary->status = FUNCTION_UNANALYZED;
    summary->c_support = FUNCTION_C_UNKNOWN;
}

const function_summary_t *snapshot_function_summary(arena_t *arena,
                                                    const function_summary_t *summary) {
    function_summary_t *copy =
        create_function_summary(arena, summary->function, summary->parameter_count);
    const lattice_element_t **parameters = copy->parameter_types;
    *copy = *summary;
    copy->next = NULL;
    copy->parameter_types = parameters;
    if (summary->parameter_count)
        memcpy(parameters,
               summary->parameter_types,
               summary->parameter_count * sizeof(*parameters));
    return copy;
}

string_value_t function_summary_to_string(const function_summary_t *summary) {
    const wchar_t *status = summary->status == FUNCTION_UNANALYZED  ? L"unanalyzed"
                            : summary->status == FUNCTION_ANALYZING ? L"analyzing"
                            : summary->status == FUNCTION_ANALYZED  ? L"analyzed"
                                                                    : L"inconclusive";
    const wchar_t *support = summary->c_support == FUNCTION_C_SUPPORTED     ? L"supported"
                             : summary->c_support == FUNCTION_C_UNSUPPORTED ? L"unsupported"
                                                                            : L"unknown";
    string_builder_t builder;
    init_string_builder(&builder, 0);
    append_string(&builder, status);
    append_char(&builder, L' ');
    string_value_t signature = function_signature_to_string(summary);
    append_string_value(&builder, signature);
    FREE_STRING(signature);
    append_static_string(&builder, L" -> ");
    string_value_t result = lattice_to_string(summary->return_type);
    append_string_value(&builder, result);
    FREE_STRING(result);
    append_static_string(&builder, L" effects=");
    if (!summary->effects)
        append_static_string(&builder, L"none");
    else {
        bool separator = false;
        const uint32_t bits[] = {FUNCTION_EFFECT_INPUT,
                                 FUNCTION_EFFECT_OUTPUT,
                                 FUNCTION_EFFECT_EXTERNAL_READ,
                                 FUNCTION_EFFECT_EXTERNAL_WRITE,
                                 FUNCTION_EFFECT_UNKNOWN};
        const wchar_t *names[] = {L"input",
                                  L"output",
                                  L"external-read",
                                  L"external-write",
                                  L"unknown"};
        for (size_t i = 0; i < sizeof(bits) / sizeof(*bits); i++) {
            if (summary->effects & bits[i]) {
                if (separator)
                    append_char(&builder, L'|');
                append_string(&builder, names[i]);
                separator = true;
            }
        }
    }
    append_static_string(&builder, L" c=");
    return append_string(&builder, support);
}

function_summary_set_t *
create_function_summary_set(arena_t *arena, const node_t *function, size_t parameter_count) {
    function_summary_set_t *set = alloc_zeroed_from_arena(arena, sizeof(*set));
    set->arena = arena;
    set->function = function;
    set->parameter_count = parameter_count;
    return set;
}

void reset_function_summary_set(function_summary_set_t *set) {
    set->head = set->tail = NULL;
}

/** @brief Removes value refinements and identities from a specialization key. */
const lattice_element_t *function_summary_type(const lattice_element_t *value) {
    switch (value->type) {
        case LATTICE_INTEGER_CONSTANT:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER:
            return make_integer_element();
        case LATTICE_REAL_CONSTANT:
        case LATTICE_REAL:
            return make_real_element();
        case LATTICE_STRING_CONSTANT:
        case LATTICE_STRING:
            return make_string_element();
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_BOOLEAN:
            return make_boolean_element();
        case LATTICE_KNOWN_FUNCTION:
        case LATTICE_FUNCTION:
            return make_function_element();
        case LATTICE_TYPED_ARRAY:
        case LATTICE_ARRAY:
            return make_array_element();
        case LATTICE_BOTTOM:
            return make_bottom_element();
        case LATTICE_NULL:
            return make_null_element();
        case LATTICE_NUMERIC:
            return make_numeric_element();
        case LATTICE_NOT_NULL:
            return make_not_null_element();
        case LATTICE_USER_DEFINED_OBJECT:
            return make_user_defined_object_element();
        default:
            return make_top_element();
    }
}

function_summary_t *register_function_specialization(function_summary_set_t *set,
                                                     const lattice_element_t *const *args,
                                                     size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (args[i]->type == LATTICE_BOTTOM)
            return NULL;
    }
    for (function_summary_t *summary = set->head; summary; summary = summary->next) {
        size_t i = 0;
        while (i < set->parameter_count
               && summary->parameter_types[i]
                      == function_summary_type(i < count ? args[i] : make_null_element()))
            i++;
        if (i == set->parameter_count)
            return summary;
    }
    function_summary_t *summary =
        create_function_summary(set->arena, set->function, set->parameter_count);
    for (size_t i = 0; i < set->parameter_count; i++)
        summary->parameter_types[i] =
            function_summary_type(i < count ? args[i] : make_null_element());
    if (set->tail)
        set->tail->next = summary;
    else
        set->head = summary;
    set->tail = summary;
    return summary;
}

string_value_t function_signature_to_string(const function_summary_t *summary) {
    string_builder_t builder;
    init_string_builder(&builder, 0);
    append_char(&builder, L'(');
    for (size_t i = 0; i < summary->parameter_count; i++) {
        if (i)
            append_static_string(&builder, L", ");
        string_value_t type = lattice_to_string(summary->parameter_types[i]);
        append_string_value(&builder, type);
        FREE_STRING(type);
    }
    return append_char(&builder, L')');
}
