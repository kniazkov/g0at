/** @file c_strings.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native str calls and static wide-string storage.
 */
#include "c_strings.h"

#include "c_arithmetic.h"
#include "c_lowering.h"
#include "graph/node.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

string_value_t c_string_from_value(source_builder_t *prelude,
                                   c_generation_context_t *context,
                                   c_value_type_t type,
                                   const wchar_t *value) {
    if (type == C_VALUE_STRING)
        return format_string(L"%s", value);
    const wchar_t *suffix = type == C_VALUE_INT64    ? L"i64"
                            : type == C_VALUE_DOUBLE ? L"f64"
                            : type == C_VALUE_BOOL   ? L"bool"
                            : type == C_VALUE_CHAR   ? L"char"
                                                     : NULL;
    if (!suffix || !context->module_definition)
        return (string_value_t){0};
    string_value_t buffer = format_string(L"g_t%zu", context->temporary_count++);
    add_source(prelude, 0, L"goat_native_string_buffer_v2_t %s = {0};", buffer.data);
    add_source(prelude,
               0,
               L"if (goat_native_str_%s(g_context, %s, &%s) != GOAT_NATIVE_OK) "
               L"longjmp(g_guard->recovery, 1);",
               suffix,
               value,
               buffer.data);
    string_value_t result = format_string(L"goat_native_string_view(%s)", buffer.data);
    FREE_STRING(buffer);
    return result;
}

c_generated_expression_t c_string_call(const node_t *node, c_generation_context_t *context) {
    if (get_node_child_count(node) < 2 || !context->module_definition
        || c_generation_expression_type(context, node) != C_VALUE_STRING) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    source_builder_t *prelude = create_source_builder();
    string_value_t first = {0};
    c_value_type_t type = C_VALUE_UNKNOWN;
    for (size_t i = 1; i < get_node_child_count(node); i++) {
        c_generated_expression_t arg = generate_c_code_from_node(get_node_child(node, i), context);
        if (!arg.success || !c_type_name(arg.type)) {
            destroy_c_expression(&arg);
            FREE_STRING(first);
            destroy_source_builder(prelude);
            fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
            return (c_generated_expression_t){0};
        }
        string_value_t captured = c_capture_operand(prelude, context, &arg, arg.type);
        if (i == 1) {
            first = captured;
            type = arg.type;
        } else {
            add_source(prelude, 0, L"(void)%s;", captured.data);
            FREE_STRING(captured);
        }
        destroy_c_expression(&arg);
    }
    string_value_t value = c_string_from_value(prelude, context, type, first.data);
    FREE_STRING(first);
    if (!value.data) {
        destroy_source_builder(prelude);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    return (c_generated_expression_t){.success = true,
                                      .type = C_VALUE_STRING,
                                      .value = value,
                                      .prelude = prelude};
}

c_generated_expression_t
c_string_literal(const node_t *node, c_generation_context_t *context, string_view_t value) {
    if (c_generation_expression_type(context, node) != C_VALUE_STRING) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    source_builder_t *prelude = create_source_builder();
    string_value_t name = format_string(L"g_t%zu", context->temporary_count++);
    string_builder_t units;
    init_string_builder(&units, 64);
    for (size_t i = 0; i < value.length; i++) {
        string_value_t unit = format_string(L"(wchar_t)%u,", (unsigned)(uint32_t)value.data[i]);
        append_string_value(&units, unit);
        FREE_STRING(unit);
    }
    string_value_t contents = append_string(&units, L"0");
    add_source(prelude, 0, L"static const wchar_t %s[] = {%s};", name.data, contents.data);
    string_value_t result =
        format_string(L"((goat_native_string_view_v2_t){%s, %zu})", name.data, value.length);
    FREE_STRING(contents);
    FREE_STRING(name);
    return (c_generated_expression_t){.success = true,
                                      .type = C_VALUE_STRING,
                                      .value = result,
                                      .prelude = prelude};
}
