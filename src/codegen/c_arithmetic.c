/** @file c_arithmetic.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Saturating integers and sequenced binary64 expression evaluation.
 */
#include "c_arithmetic.h"

#include "c_lowering.h"
#include "graph/node.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

static bool numeric(c_value_type_t type) {
    return type == C_VALUE_INT64 || type == C_VALUE_DOUBLE;
}

/** @brief Copies prelude statements before evaluating and rounding the operand once. */
string_value_t c_capture_operand(source_builder_t *prelude,
                                 c_generation_context_t *context,
                                 const c_generated_expression_t *operand,
                                 c_value_type_t type) {
    for (size_t i = 0; operand->prelude && i < operand->prelude->count; i++) {
        const line_of_code_t *line = &operand->prelude->lines[i];
        add_source(prelude, line->indent, L"%s", line->text.data);
    }
    string_value_t name = format_string(L"g_t%zu", context->temporary_count++);
    add_source(prelude,
               0,
               L"%s%s %s = (%s)(%s);",
               type == C_VALUE_DOUBLE ? L"volatile " : L"",
               c_type_name(type),
               name.data,
               c_type_name(type),
               operand->value.data);
    return name;
}

c_generated_expression_t
c_binary_arithmetic(const node_t *node, c_generation_context_t *context, wchar_t operation) {
    if (operation != L'+' && operation != L'-' && operation != L'*') {
        fail_c_generation(context, node, C_GENERATION_UNSUPPORTED);
        return (c_generated_expression_t){0};
    }
    c_generated_expression_t left = generate_c_code_from_node(get_node_child(node, 0), context);
    if (!left.success)
        return left;
    c_generated_expression_t right = generate_c_code_from_node(get_node_child(node, 1), context);
    if (!right.success) {
        destroy_c_expression(&left);
        return right;
    }
    c_value_type_t type =
        left.type == C_VALUE_INT64 && right.type == C_VALUE_INT64 ? C_VALUE_INT64 : C_VALUE_DOUBLE;
    if (!numeric(left.type) || !numeric(right.type)
        || c_generation_expression_type(context, node) != type) {
        destroy_c_expression(&left);
        destroy_c_expression(&right);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    source_builder_t *prelude = create_source_builder();
    string_value_t a = c_capture_operand(prelude, context, &left, type);
    string_value_t b = c_capture_operand(prelude, context, &right, type);
    string_value_t value = format_string(L"g_t%zu", context->temporary_count++);
    if (type == C_VALUE_INT64) {
        context->helper_flags |= operation == L'+'   ? C_HELPER_I64_ADD
                                 : operation == L'-' ? C_HELPER_I64_SUB
                                                     : C_HELPER_I64_MUL;
        add_source(prelude,
                   0,
                   L"int64_t %s = g_i64_%s(%s, %s);",
                   value.data,
                   operation == L'+'   ? L"add"
                   : operation == L'-' ? L"sub"
                                       : L"mul",
                   a.data,
                   b.data);
    } else
        add_source(prelude,
                   0,
                   L"volatile double %s = %s %c %s;",
                   value.data,
                   a.data,
                   operation,
                   b.data);
    FREE_STRING(a);
    FREE_STRING(b);
    destroy_c_expression(&left);
    destroy_c_expression(&right);
    return (c_generated_expression_t){.success = true,
                                      .type = type,
                                      .value = value,
                                      .prelude = prelude};
}

c_generated_expression_t
c_unary_arithmetic(const node_t *node, c_generation_context_t *context, bool negative) {
    c_generated_expression_t operand = generate_c_code_from_node(get_node_child(node, 0), context);
    if (!operand.success)
        return operand;
    if (!numeric(operand.type) || c_generation_expression_type(context, node) != operand.type) {
        destroy_c_expression(&operand);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    if (!negative)
        return operand;
    source_builder_t *prelude = create_source_builder();
    string_value_t argument = c_capture_operand(prelude, context, &operand, operand.type);
    string_value_t value = format_string(L"g_t%zu", context->temporary_count++);
    if (operand.type == C_VALUE_INT64) {
        context->helper_flags |= C_HELPER_I64_NEG;
        add_source(prelude, 0, L"int64_t %s = g_i64_neg(%s);", value.data, argument.data);
    } else
        add_source(prelude, 0, L"volatile double %s = -%s;", value.data, argument.data);
    c_value_type_t type = operand.type;
    abstract_truth_t truth = operand.literal_truth;
    FREE_STRING(argument);
    destroy_c_expression(&operand);
    return (c_generated_expression_t){.success = true,
                                      .type = type,
                                      .literal_truth = truth,
                                      .value = value,
                                      .prelude = prelude};
}

c_generated_expression_t c_parenthesized(const node_t *node, c_generation_context_t *context) {
    c_generated_expression_t operand = generate_c_code_from_node(get_node_child(node, 0), context);
    if (operand.success && c_generation_expression_type(context, node) != operand.type) {
        destroy_c_expression(&operand);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
    }
    return operand;
}

void c_arithmetic_helpers(source_builder_t *builder, unsigned helpers) {
    {
        static const wchar_t *const configuration[] = {
            L"",
            L"#ifndef GOAT_C_NUMERIC_HELPERS",
            L"#define GOAT_C_NUMERIC_HELPERS",
            L"/** @brief Selects overflow intrinsics when supported; tests may force the portable "
            L"path. */",
            L"#if !defined(GOAT_FORCE_PORTABLE_INTEGER_MATH)",
            L"#if defined(__has_builtin)",
            L"#if __has_builtin(__builtin_add_overflow) && __has_builtin(__builtin_sub_overflow) "
            L"&& __has_builtin(__builtin_mul_overflow)",
            L"#define GOAT_INTEGER_OVERFLOW_BUILTINS 1",
            L"#endif",
            L"#elif defined(__GNUC__) && __GNUC__ >= 5",
            L"#define GOAT_INTEGER_OVERFLOW_BUILTINS 1",
            L"#endif",
            L"#endif",
            L"",
            L"#endif",
        };
        add_source_lines(builder, 0, configuration, sizeof(configuration) / sizeof(*configuration));
    }

    const struct {
        unsigned flag;
        const wchar_t *name;
        const wchar_t *guard;
    } binary[] = {{C_HELPER_I64_ADD, L"add", L"ADD"},
                  {C_HELPER_I64_SUB, L"sub", L"SUB"},
                  {C_HELPER_I64_MUL, L"mul", L"MUL"}};

    for (size_t i = 0; i < sizeof(binary) / sizeof(*binary); i++) {
        if (!(helpers & binary[i].flag))
            continue;
        add_static_source(builder, 0, L"");
        add_source(builder, 0, L"#ifndef GOAT_C_I64_%s", binary[i].guard);
        add_source(builder, 0, L"#define GOAT_C_I64_%s", binary[i].guard);
        add_source(builder,
                   0,
                   L"static inline int64_t g_i64_%s(int64_t a, int64_t b) {",
                   binary[i].name);
        if (i == 0) {
            static const wchar_t *const addition[] = {
                L"#ifdef GOAT_INTEGER_OVERFLOW_BUILTINS",
                L"    int64_t result;",
                L"    if (__builtin_add_overflow(a, b, &result))",
                L"        return a >= 0 ? INT64_MAX : INT64_MIN;",
                L"    return result;",
                L"#else",
                L"    if (b > 0 && a > INT64_MAX - b) return INT64_MAX;",
                L"    if (b < 0 && a < INT64_MIN - b) return INT64_MIN;",
                L"    return a + b;",
                L"#endif",
            };
            add_source_lines(builder, 0, addition, sizeof(addition) / sizeof(*addition));
        } else if (i == 1) {
            static const wchar_t *const subtraction[] = {
                L"#ifdef GOAT_INTEGER_OVERFLOW_BUILTINS",
                L"    int64_t result;",
                L"    if (__builtin_sub_overflow(a, b, &result))",
                L"        return a >= 0 ? INT64_MAX : INT64_MIN;",
                L"    return result;",
                L"#else",
                L"    if (b < 0 && a > INT64_MAX + b) return INT64_MAX;",
                L"    if (b > 0 && a < INT64_MIN + b) return INT64_MIN;",
                L"    return a - b;",
                L"#endif",
            };
            add_source_lines(builder, 0, subtraction, sizeof(subtraction) / sizeof(*subtraction));
        } else if (i == 2) {
            static const wchar_t *const multiplication[] = {
                L"#ifdef GOAT_INTEGER_OVERFLOW_BUILTINS",
                L"    int64_t result;",
                L"    if (__builtin_mul_overflow(a, b, &result))",
                L"        return (a < 0) == (b < 0) ? INT64_MAX : INT64_MIN;",
                L"    return result;",
                L"#else",
                L"    uint64_t x = a < 0 ? UINT64_C(0) - (uint64_t)a : (uint64_t)a;",
                L"    uint64_t y = b < 0 ? UINT64_C(0) - (uint64_t)b : (uint64_t)b;",
                L"    int negative = (a < 0) != (b < 0);",
                L"    uint64_t limit = negative ? (uint64_t)INT64_MAX + 1 : "
                L"(uint64_t)INT64_MAX;",
                L"    if (y != 0 && x > limit / y) return negative ? INT64_MIN : INT64_MAX;",
                L"    uint64_t product = x * y;",
                L"    if (negative && product == (uint64_t)INT64_MAX + 1) return INT64_MIN;",
                L"    return negative ? -(int64_t)product : (int64_t)product;",
                L"#endif",
            };
            add_source_lines(builder,
                             0,
                             multiplication,
                             sizeof(multiplication) / sizeof(*multiplication));
        }
        add_static_source(builder, 0, L"}");
        add_static_source(builder, 0, L"#endif");
    }
    if (helpers & C_HELPER_I64_NEG) {
        static const wchar_t *const negation[] = {
            L"",
            L"#ifndef GOAT_C_I64_NEG",
            L"#define GOAT_C_I64_NEG",
            L"static inline int64_t g_i64_neg(int64_t value) {",
        };
        add_source_lines(builder, 0, negation, sizeof(negation) / sizeof(*negation));
        add_static_source(builder, 1, L"return value == INT64_MIN ? INT64_MAX : -value;");
        add_static_source(builder, 0, L"}");
        add_static_source(builder, 0, L"#endif");
    }
}
