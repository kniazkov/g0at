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

/** @brief Maximum number of operands retained by a native call lowering. */
#define NATIVE_MAX_ARGUMENTS 2

/** @brief Decides whether an evaluated operand may be passed to a generator. */
typedef bool (*native_argument_rule_t)(c_value_type_t type,
                                       size_t index,
                                       c_value_type_t result_type);

/** @brief Accepts any operand with a named C type, matching the first to the result type. */
static bool accept_abs_argument(c_value_type_t type, size_t index, c_value_type_t result_type) {
    if (index == 1)
        return type == result_type;
    return c_type_name(type) != NULL;
}

/** @brief Accepts only numeric operands, as required by libm calls. */
static bool accept_numeric_argument(c_value_type_t type, size_t index, c_value_type_t result_type) {
    (void)index;
    (void)result_type;
    return type == C_VALUE_INT64 || type == C_VALUE_DOUBLE;
}

/** @brief Accepts a numeric source operand; later operands only need a named C type. */
static bool accept_int_argument(c_value_type_t type, size_t index, c_value_type_t result_type) {
    (void)result_type;
    if (index == 1)
        return type == C_VALUE_INT64 || type == C_VALUE_DOUBLE;
    return c_type_name(type) != NULL;
}

/**
 * @brief Generates code for call arguments, retaining the first @p max_args operands
 *        and evaluating any remaining ones for their effects only.
 * @return true when every operand satisfied the acceptance rule.
 */
static bool capture_native_arguments(const node_t *node,
                                     c_generation_context_t *context,
                                     size_t max_args,
                                     native_argument_rule_t accepts,
                                     c_value_type_t result_type,
                                     string_value_t *out,
                                     source_builder_t *prelude) {
    size_t count = get_node_child_count(node) - 1;
    bool success = true;
    for (size_t i = count; i > 0 && success; i--) {
        c_generated_expression_t argument =
            generate_c_code_from_node(get_node_child(node, i), context);
        success = argument.success && accepts(argument.type, i, result_type);
        if (success) {
            string_value_t captured = c_capture_operand(prelude, context, &argument, argument.type);
            if (i <= max_args)
                out[i - 1] = captured;
            else {
                add_source(prelude, 0, L"(void)%s;", captured.data);
                FREE_STRING(captured);
            }
        }
        destroy_c_expression(&argument);
    }
    return success;
}

/** @brief Releases captured operand text. */
static void free_native_arguments(string_value_t *arguments, size_t count) {
    for (size_t i = 0; i < count; i++)
        FREE_STRING(arguments[i]);
}

/** @brief Emits an arity-1 or arity-2 libm call rounded to binary64. */
static void emit_native_call(source_builder_t *prelude,
                             c_generation_context_t *context,
                             const wchar_t *name,
                             const string_value_t *arguments,
                             size_t arity,
                             const wchar_t *value) {
    if (arity == 2) {
        string_value_t a = format_string(L"g_t%zu", context->temporary_count++);
        string_value_t b = format_string(L"g_t%zu", context->temporary_count++);
        add_source(prelude, 0, L"double %s = %s;", a.data, arguments[0].data);
        add_source(prelude, 0, L"double %s = %s;", b.data, arguments[1].data);
        add_source(prelude, 0, L"volatile double %s = %s(%s, %s);", value, name, a.data, b.data);
        FREE_STRING(a);
        FREE_STRING(b);
    } else {
        add_source(prelude, 0, L"volatile double %s = %s(%s);", value, name, arguments[0].data);
    }
}

/** @brief Lowers proven abs calls, preserving unused argument effects and binary64 rounding. */
static c_generated_expression_t native_abs(const node_t *node, c_generation_context_t *context) {
    size_t count = get_node_child_count(node) - 1;
    c_value_type_t type = c_generation_expression_type(context, node);
    if (!count || (type != C_VALUE_INT64 && type != C_VALUE_DOUBLE)) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    source_builder_t *prelude = create_source_builder();
    string_value_t arguments[NATIVE_MAX_ARGUMENTS] = {0};
    if (!capture_native_arguments(node,
                                  context,
                                  1,
                                  accept_abs_argument,
                                  type,
                                  arguments,
                                  prelude)) {
        free_native_arguments(arguments, 1);
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
                   arguments[0].data,
                   arguments[0].data,
                   arguments[0].data);
    } else {
        add_source(prelude, 0, L"volatile double %s = fabs(%s);", value.data, arguments[0].data);
    }
    free_native_arguments(arguments, 1);
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
    string_value_t arguments[NATIVE_MAX_ARGUMENTS] = {0};
    if (!capture_native_arguments(node,
                                  context,
                                  2,
                                  accept_numeric_argument,
                                  C_VALUE_DOUBLE,
                                  arguments,
                                  prelude)) {
        free_native_arguments(arguments, 2);
        destroy_source_builder(prelude);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    string_value_t value = format_string(L"g_t%zu", context->temporary_count++);
    emit_native_call(prelude,
                     context,
                     count == 1 ? L"atan" : L"atan2",
                     arguments,
                     count > 1 ? 2 : 1,
                     value.data);
    free_native_arguments(arguments, 2);
    return (c_generated_expression_t){.success = true,
                                      .type = C_VALUE_DOUBLE,
                                      .value = value,
                                      .prelude = prelude};
}

/** @brief Shared libm lowering; min_args fixes the arity and c_name the C function. */
static c_generated_expression_t native_libm(const node_t *node,
                                            c_generation_context_t *context,
                                            const builtin_function_t *builtin,
                                            const wchar_t *c_name) {
    size_t count = get_node_child_count(node) - 1;
    if (count < builtin->min_args
        || c_generation_expression_type(context, node) != C_VALUE_DOUBLE) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    source_builder_t *prelude = create_source_builder();
    string_value_t arguments[NATIVE_MAX_ARGUMENTS] = {0};
    if (!capture_native_arguments(node,
                                  context,
                                  builtin->min_args,
                                  accept_numeric_argument,
                                  C_VALUE_DOUBLE,
                                  arguments,
                                  prelude)) {
        free_native_arguments(arguments, builtin->min_args);
        destroy_source_builder(prelude);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    string_value_t value = format_string(L"g_t%zu", context->temporary_count++);
    emit_native_call(prelude, context, c_name, arguments, builtin->min_args, value.data);
    free_native_arguments(arguments, builtin->min_args);
    return (c_generated_expression_t){.success = true,
                                      .type = C_VALUE_DOUBLE,
                                      .value = value,
                                      .prelude = prelude};
}

/** @brief Lowers proven int casts, preserving fallback effects and the VM range check. */
static c_generated_expression_t native_int(const node_t *node, c_generation_context_t *context) {
    size_t count = get_node_child_count(node) - 1;
    if (!count || c_generation_expression_type(context, node) != C_VALUE_INT64) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    c_value_type_t arg_type = c_generation_expression_type(context, get_node_child(node, 1));
    if (arg_type != C_VALUE_INT64 && arg_type != C_VALUE_DOUBLE) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    bool double_arg = arg_type == C_VALUE_DOUBLE;
    if (double_arg && count >= 2
        && c_generation_expression_type(context, get_node_child(node, 2)) != C_VALUE_INT64) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    source_builder_t *prelude = create_source_builder();
    string_value_t arguments[NATIVE_MAX_ARGUMENTS] = {0};
    if (!capture_native_arguments(node,
                                  context,
                                  2,
                                  accept_int_argument,
                                  C_VALUE_INT64,
                                  arguments,
                                  prelude)) {
        free_native_arguments(arguments, 2);
        destroy_source_builder(prelude);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    if (double_arg) {
        string_value_t value = format_string(L"g_t%zu", context->temporary_count++);
        add_source(prelude,
                   0,
                   L"int64_t %s = (isfinite(%s) && %s >= -0x1p63 && %s < 0x1p63)"
                   L" ? (int64_t)%s : %s;",
                   value.data,
                   arguments[0].data,
                   arguments[0].data,
                   arguments[0].data,
                   arguments[0].data,
                   count >= 2 ? arguments[1].data : L"INT64_C(0)");
        free_native_arguments(arguments, 2);
        return (c_generated_expression_t){.success = true,
                                          .type = C_VALUE_INT64,
                                          .value = value,
                                          .prelude = prelude};
    }
    if (count >= 2)
        add_source(prelude, 0, L"(void)%s;", arguments[1].data);
    FREE_STRING(arguments[1]);
    return (c_generated_expression_t){.success = true,
                                      .type = C_VALUE_INT64,
                                      .value = arguments[0],
                                      .prelude = prelude};
}

/** @brief Lowers proven sign calls; NaN and both signed zeros map to zero. */
static c_generated_expression_t native_sign(const node_t *node, c_generation_context_t *context) {
    size_t count = get_node_child_count(node) - 1;
    if (!count || c_generation_expression_type(context, node) != C_VALUE_INT64) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    source_builder_t *prelude = create_source_builder();
    string_value_t arguments[NATIVE_MAX_ARGUMENTS] = {0};
    if (!capture_native_arguments(node,
                                  context,
                                  1,
                                  accept_numeric_argument,
                                  C_VALUE_INT64,
                                  arguments,
                                  prelude)) {
        free_native_arguments(arguments, 1);
        destroy_source_builder(prelude);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    string_value_t value = format_string(L"g_t%zu", context->temporary_count++);
    add_source(prelude,
               0,
               L"int64_t %s = %s > 0 ? 1 : %s < 0 ? -1 : 0;",
               value.data,
               arguments[0].data,
               arguments[0].data);
    free_native_arguments(arguments, 1);
    return (c_generated_expression_t){.success = true,
                                      .type = C_VALUE_INT64,
                                      .value = value,
                                      .prelude = prelude};
}

/** @brief One registered built-in; a NULL generator selects the shared libm lowering. */
typedef struct c_native_entry_t {
    const builtin_function_t *builtin;
    c_native_generator_t generator;
    const wchar_t *c_name; /**< C function name; NULL means the Goat name is used. */
} c_native_entry_t;

static const c_native_entry_t generators[] = {
    {&builtin_abs, native_abs, NULL}, {&builtin_atan, native_atan, NULL},
    {&builtin_acos, NULL, NULL},      {&builtin_asin, NULL, NULL},
    {&builtin_cbrt, NULL, NULL},      {&builtin_ceil, NULL, NULL},
    {&builtin_cos, NULL, NULL},       {&builtin_cosh, NULL, NULL},
    {&builtin_exp, NULL, NULL},       {&builtin_exp2, NULL, NULL},
    {&builtin_expm1, NULL, NULL},     {&builtin_floor, NULL, NULL},
    {&builtin_fmod, NULL, NULL},      {&builtin_hypot, NULL, NULL},
    {&builtin_int, native_int, NULL}, {&builtin_log, NULL, NULL},
    {&builtin_log10, NULL, NULL},     {&builtin_log1p, NULL, NULL},
    {&builtin_log2, NULL, NULL},      {&builtin_max, NULL, L"fmax"},
    {&builtin_min, NULL, L"fmin"},    {&builtin_pow, NULL, NULL},
    {&builtin_round, NULL, NULL},     {&builtin_sign, native_sign, NULL},
    {&builtin_sin, NULL, NULL},       {&builtin_sinh, NULL, NULL},
    {&builtin_sqrt, NULL, NULL},      {&builtin_tan, NULL, NULL},
    {&builtin_tanh, NULL, NULL},      {&builtin_trunc, NULL, NULL}};

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
    return native_libm(node, context, builtin, entry->c_name ? entry->c_name : builtin->name);
}
