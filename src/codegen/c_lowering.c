/** @file c_lowering.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Numeric literals, parameter signatures and explicit returns.
 */
#include "c_lowering.h"

#include "c_arithmetic.h"
#include "graph/replacement.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

const wchar_t *c_type_name(c_value_type_t type) {
    return type == C_VALUE_INT64 ? L"int64_t" : type == C_VALUE_DOUBLE ? L"double" : NULL;
}

static c_generated_expression_t literal(const node_t *node,
                                        c_generation_context_t *context,
                                        c_value_type_t type,
                                        string_value_t value) {
    if (c_generation_expression_type(context, node) != type) {
        FREE_STRING(value);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    return (c_generated_expression_t){.success = true, .type = type, .value = value};
}

c_generated_expression_t
c_integer_literal(const node_t *node, c_generation_context_t *context, int64_t value) {
    string_value_t text = value == INT64_MIN ? STATIC_STRING(L"(-INT64_C(9223372036854775807)-1)")
                          : value < 0        ? format_string(L"(-INT64_C(%ld))", -value)
                                             : format_string(L"INT64_C(%ld)", value);
    return literal(node, context, C_VALUE_INT64, text);
}

c_generated_expression_t
c_real_literal(const node_t *node, c_generation_context_t *context, double value) {
    if (sizeof(double) != sizeof(uint64_t) || FLT_RADIX != 2 || DBL_MANT_DIG != 53
        || DBL_MAX_EXP != 1024) {
        fail_c_generation(context, node, C_GENERATION_UNSUPPORTED);
        return (c_generated_expression_t){0};
    }
    if (isnan(value))
        return literal(node, context, C_VALUE_DOUBLE, STATIC_STRING(L"((double)NAN)"));
    if (isinf(value))
        return literal(node,
                       context,
                       C_VALUE_DOUBLE,
                       signbit(value) ? STATIC_STRING(L"(-(double)INFINITY)")
                                      : STATIC_STRING(L"((double)INFINITY)"));
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    unsigned exponent = (unsigned)((bits >> 52) & 2047);
    char text[80];
    snprintf(text,
             sizeof(text),
             "%s0x%u.%013llxp%d",
             bits >> 63 ? "-" : "",
             exponent ? 1 : 0,
             (unsigned long long)(bits & UINT64_C(0xfffffffffffff)),
             exponent ? (int)exponent - 1023 : -1022);
    return literal(node, context, C_VALUE_DOUBLE, format_string(L"(%a)", text));
}

bool c_emit_return(const node_t *node,
                   c_generation_context_t *context,
                   source_builder_t *builder,
                   size_t indent) {
    const node_t *value = get_node_child(node, 0);
    if (!value)
        return fail_c_generation(context, node, C_GENERATION_UNSUPPORTED);
    c_generated_expression_t expression = generate_c_code_from_node(value, context);
    if (!expression.success)
        return false;
    bool valid = expression.type == c_generation_return_type(context);
    if (valid) {
        for (size_t i = 0; expression.prelude && i < expression.prelude->count; i++) {
            const line_of_code_t *line = &expression.prelude->lines[i];
            add_source(builder, indent + line->indent, L"%s", line->text.data);
        }
        add_source(builder, indent, L"return %s;", expression.value.data);
    }
    destroy_c_expression(&expression);
    return valid || fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
}

bool c_emit_body(const node_t *node,
                 c_generation_context_t *context,
                 source_builder_t *builder,
                 size_t indent) {
    /* Control-flow lowering comes later; a single explicit return is the initial subset. */
    if (get_node_child_count(node) != 1
        || replacement_original(get_node_child(node, 0))->vtbl->type != NODE_RETURN)
        return fail_c_generation(context, node, C_GENERATION_UNSUPPORTED);
    return generate_indented_c_code_from_node(get_node_child(node, 0), context, builder, indent);
}

bool c_emit_function(const node_t *node,
                     c_generation_context_t *context,
                     source_builder_t *builder,
                     size_t indent) {
    if (node != context->summary->function)
        return fail_c_generation(context, node, C_GENERATION_UNSUPPORTED);
    size_t count = context->summary->parameter_count;
    const node_t *parameters = get_node_child(node, 0);
    if (get_node_child_count(parameters) != count)
        return fail_c_generation(context, node, C_GENERATION_INVALID_REQUEST);
    c_generation_binding_t *bindings = count ? CALLOC(count * sizeof(*bindings)) : NULL;
    const c_generation_binding_t *saved = context->bindings;
    string_builder_t signature;
    init_string_builder(&signature, 64);
    append_string(&signature, c_type_name(c_generation_return_type(context)));
    append_char(&signature, L' ');
    append_substring(&signature, context->function_name.data, context->function_name.length);
    append_char(&signature, L'(');
    if (!count)
        append_string(&signature, L"void");
    for (size_t i = 0; i < count; i++) {
        string_value_t name = format_string(L"goat_p%zu", i);
        bindings[i] = (c_generation_binding_t){.next = i ? &bindings[i - 1] : saved,
                                               .declaration = get_node_child(parameters, i),
                                               .name = {name.data, name.length},
                                               .type = c_generation_parameter_type(context, i)};
        if (i)
            append_string(&signature, L", ");
        append_string(&signature, c_type_name(bindings[i].type));
        append_char(&signature, L' ');
        append_string_value(&signature, name);
    }
    if (count)
        context->bindings = &bindings[count - 1];
    add_static_source(builder, 0, L"#include <stdint.h>");
    add_static_source(builder, 0, L"#include <math.h>");
    add_static_source(builder, 0, L"#include <float.h>");
    add_static_source(builder,
                      0,
                      L"_Static_assert(sizeof(double)==8 && FLT_RADIX==2 && DBL_MANT_DIG==53 && "
                      L"DBL_MAX_EXP==1024, \"binary64 required\");");
    c_arithmetic_helpers(builder);
    add_formatted_source(builder, indent, append_string(&signature, L") {"));
    for (size_t i = 0; i < count; i++)
        add_source(builder, indent + 1, L"(void)%s;", bindings[i].name.data);
    bool success =
        generate_indented_c_code_from_node(get_node_child(node, 1), context, builder, indent + 1);
    if (success)
        add_static_source(builder, indent, L"}");
    context->bindings = saved;
    for (size_t i = 0; i < count; i++)
        FREE((void *)bindings[i].name.data);
    FREE(bindings);
    return success;
}
