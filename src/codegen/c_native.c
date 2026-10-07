/** @file c_native.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Descriptor-keyed native built-in generators and shared libm lowering.
 */
#include "c_native.h"

#include "builtins/registry.h"
#include "c_arithmetic.h"
#include "c_lowering.h"
#include "graph/replacement.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

#include <wchar.h>

typedef c_generated_expression_t (*c_native_generator_t)(const node_t *node,
                                                         c_generation_context_t *context);

/** @brief Lowers proven abs calls, preserving unused argument effects and binary64 rounding. */
static c_generated_expression_t native_abs(const node_t *node, c_generation_context_t *context) {
    size_t count = get_node_child_count(node) - 1;
    c_value_type_t type = c_generation_expression_type(context, node);
    if (!count || (type != C_VALUE_INT64 && type != C_VALUE_DOUBLE)) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    source_builder_t *prelude = create_source_builder();
    string_value_t input = {0};
    bool success = true;
    for (size_t i = count; i > 0 && success; i--) {
        c_generated_expression_t argument =
            generate_c_code_from_node(get_node_child(node, i), context);
        success =
            argument.success && c_type_name(argument.type) && (i != 1 || argument.type == type);
        if (success) {
            string_value_t captured = c_capture_operand(prelude, context, &argument, argument.type);
            if (i == 1)
                input = captured;
            else {
                add_source(prelude, 0, L"(void)%s;", captured.data);
                FREE_STRING(captured);
            }
        }
        destroy_c_expression(&argument);
    }
    if (!success) {
        FREE_STRING(input);
        destroy_source_builder(prelude);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    string_value_t value = format_string(L"g_t%zu", context->temporary_count++);
    if (type == C_VALUE_INT64) {
        context->helper_flags |= C_HELPER_I64_NEG;
        add_source(prelude,
                   0,
                   L"int64_t %s = %s < 0 ? g_i64_neg(%s) : %s;",
                   value.data,
                   input.data,
                   input.data,
                   input.data);
    } else {
        add_source(prelude, 0, L"volatile double %s = fabs(%s);", value.data, input.data);
    }
    FREE_STRING(input);
    return (c_generated_expression_t){.success = true,
                                      .type = type,
                                      .value = value,
                                      .prelude = prelude};
}

/** @brief Lowers proven atan/atan2 calls, preserving unused argument effects and binary64 rounding.
 */
static c_generated_expression_t native_atan(const node_t *node, c_generation_context_t *context) {
    size_t count = get_node_child_count(node) - 1;
    if (!count || c_generation_expression_type(context, node) != C_VALUE_DOUBLE) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    source_builder_t *prelude = create_source_builder();
    string_value_t arguments[2] = {0};
    bool success = true;
    for (size_t i = count; i > 0 && success; i--) {
        c_generated_expression_t argument =
            generate_c_code_from_node(get_node_child(node, i), context);
        success = argument.success && c_type_name(argument.type)
                  && (argument.type == C_VALUE_INT64 || argument.type == C_VALUE_DOUBLE);
        if (success) {
            string_value_t captured = c_capture_operand(prelude, context, &argument, argument.type);
            if (i <= 2)
                arguments[i - 1] = captured;
            else {
                add_source(prelude, 0, L"(void)%s;", captured.data);
                FREE_STRING(captured);
            }
        }
        destroy_c_expression(&argument);
    }
    if (!success) {
        for (size_t i = 0; i < 2; i++)
            FREE_STRING(arguments[i]);
        destroy_source_builder(prelude);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    string_value_t value = format_string(L"g_t%zu", context->temporary_count++);
    if (count == 1)
        add_source(prelude, 0, L"volatile double %s = atan(%s);", value.data, arguments[0].data);
    else
        add_source(prelude,
                   0,
                   L"volatile double %s = atan2(%s, %s);",
                   value.data,
                   arguments[0].data,
                   arguments[1].data);
    for (size_t i = 0; i < 2; i++)
        FREE_STRING(arguments[i]);
    return (c_generated_expression_t){.success = true,
                                      .type = C_VALUE_DOUBLE,
                                      .value = value,
                                      .prelude = prelude};
}

/** @brief Shared libm lowering; the Goat name matches the C function and min_args its arity. */
static c_generated_expression_t native_libm(const node_t *node,
                                            c_generation_context_t *context,
                                            const builtin_function_t *builtin) {
    size_t count = get_node_child_count(node) - 1;
    if (count < builtin->min_args
        || c_generation_expression_type(context, node) != C_VALUE_DOUBLE) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    source_builder_t *prelude = create_source_builder();
    string_value_t arguments[2] = {0};
    bool success = true;
    for (size_t i = count; i > 0 && success; i--) {
        c_generated_expression_t argument =
            generate_c_code_from_node(get_node_child(node, i), context);
        success = argument.success && c_type_name(argument.type)
                  && (argument.type == C_VALUE_INT64 || argument.type == C_VALUE_DOUBLE);
        if (success) {
            string_value_t captured = c_capture_operand(prelude, context, &argument, argument.type);
            if (i <= builtin->min_args)
                arguments[i - 1] = captured;
            else {
                add_source(prelude, 0, L"(void)%s;", captured.data);
                FREE_STRING(captured);
            }
        }
        destroy_c_expression(&argument);
    }
    if (!success) {
        for (size_t i = 0; i < 2; i++)
            FREE_STRING(arguments[i]);
        destroy_source_builder(prelude);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    string_value_t value = format_string(L"g_t%zu", context->temporary_count++);
    if (builtin->min_args == 2)
        add_source(prelude,
                   0,
                   L"volatile double %s = %s(%s, %s);",
                   value.data,
                   builtin->name,
                   arguments[0].data,
                   arguments[1].data);
    else
        add_source(prelude,
                   0,
                   L"volatile double %s = %s(%s);",
                   value.data,
                   builtin->name,
                   arguments[0].data);
    for (size_t i = 0; i < 2; i++)
        FREE_STRING(arguments[i]);
    return (c_generated_expression_t){.success = true,
                                      .type = C_VALUE_DOUBLE,
                                      .value = value,
                                      .prelude = prelude};
}

/** @brief One registered built-in; a NULL generator selects the shared libm lowering. */
typedef struct c_native_entry_t {
    const builtin_function_t *builtin;
    c_native_generator_t generator;
} c_native_entry_t;

static const c_native_entry_t generators[] = {
    {&builtin_abs, native_abs}, {&builtin_atan, native_atan}, {&builtin_acos, NULL},
    {&builtin_asin, NULL},      {&builtin_cbrt, NULL},        {&builtin_ceil, NULL},
    {&builtin_cos, NULL},       {&builtin_cosh, NULL},        {&builtin_exp, NULL},
    {&builtin_exp2, NULL},      {&builtin_expm1, NULL},       {&builtin_floor, NULL},
    {&builtin_fmod, NULL},      {&builtin_hypot, NULL},       {&builtin_log, NULL},
    {&builtin_log10, NULL},     {&builtin_log1p, NULL},       {&builtin_log2, NULL},
    {&builtin_pow, NULL},       {&builtin_round, NULL},       {&builtin_sin, NULL},
    {&builtin_sinh, NULL},      {&builtin_sqrt, NULL},        {&builtin_tan, NULL},
    {&builtin_tanh, NULL},      {&builtin_trunc, NULL}};

static const c_native_entry_t *find_generator(const builtin_function_t *builtin) {
    for (size_t i = 0; i < sizeof(generators) / sizeof(*generators); i++) {
        if (generators[i].builtin == builtin)
            return &generators[i];
    }
    return NULL;
}

bool c_native_builtin_supported(const builtin_function_t *builtin) {
    return builtin && find_generator(builtin);
}

c_generated_expression_t c_native_call(const node_t *node,
                                       c_generation_context_t *context,
                                       const builtin_function_t *builtin) {
    const c_native_entry_t *entry = find_generator(builtin);
    if (!entry) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    if (entry->generator)
        return entry->generator(node, context);
    return native_libm(node, context, builtin);
}
