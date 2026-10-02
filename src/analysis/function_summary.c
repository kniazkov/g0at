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
    reset_function_summary(summary);
    return summary;
}

void reset_function_summary(function_summary_t *summary) {
    for (size_t i = 0; i < summary->parameter_count; i++)
        summary->parameter_types[i] = make_top_element();
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
    append_static_string(&builder, L" (");
    for (size_t i = 0; i < summary->parameter_count; i++) {
        if (i)
            append_static_string(&builder, L", ");
        string_value_t type = lattice_to_string(summary->parameter_types[i]);
        append_string_value(&builder, type);
        FREE_STRING(type);
    }
    append_static_string(&builder, L") -> ");
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
