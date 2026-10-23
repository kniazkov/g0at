/** @file c_string_methods.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Sequenced lowering of immutable string operations to the native runtime.
 */
#include "c_string_methods.h"

#include "builtins/registry.h"
#include "c_arithmetic.h"
#include "c_lowering.h"
#include "c_strings.h"
#include "goat/unicode.h"
#include "graph/member_access.h"
#include "graph/replacement.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

typedef enum { DIRECT, CHECKED, BUFFER, FORMAT } method_kind_t;

typedef struct {
    const builtin_function_t *builtin;
    const gchar_t *name;
    c_value_type_t result;
    method_kind_t kind;
    size_t min, max; /**< Counts include the receiver. */
    bool characters; /**< String operands may also be characters. */
    bool integers;   /**< Operands after the receiver are indices/counts. */
} method_t;

static const method_t methods[] = {
    {&builtin_is_empty, U"is_empty", C_VALUE_BOOL, DIRECT, 1, 1, false, false},
    {&builtin_is_blank, U"is_blank", C_VALUE_BOOL, CHECKED, 1, 1, false, false},
    {&builtin_char_at, U"char_at", C_VALUE_CHAR, CHECKED, 2, 2, false, true},
    {&builtin_substring, U"substring", C_VALUE_STRING, BUFFER, 2, 3, false, true},
    {&builtin_contains, U"contains", C_VALUE_BOOL, DIRECT, 2, 2, false, false},
    {&builtin_index_of, U"index_of", C_VALUE_INT64, DIRECT, 2, 2, true, false},
    {&builtin_last_index_of, U"last_index_of", C_VALUE_INT64, DIRECT, 2, 2, true, false},
    {&builtin_starts_with, U"starts_with", C_VALUE_BOOL, DIRECT, 2, 2, true, false},
    {&builtin_ends_with, U"ends_with", C_VALUE_BOOL, DIRECT, 2, 2, true, false},
    {&builtin_trim, U"trim", C_VALUE_STRING, BUFFER, 1, 1, false, false},
    {&builtin_to_upper_case, U"to_upper_case", C_VALUE_STRING, BUFFER, 1, 1, false, false},
    {&builtin_to_lower_case, U"to_lower_case", C_VALUE_STRING, BUFFER, 1, 1, false, false},
    {&builtin_repeat, U"repeat", C_VALUE_STRING, BUFFER, 2, 2, false, true},
    {&builtin_replace, U"replace", C_VALUE_STRING, BUFFER, 3, 3, true, false},
    {&builtin_format, U"format", C_VALUE_STRING, FORMAT, 1, SIZE_MAX, false, false}};

static const method_t *find_method(const builtin_function_t *builtin) {
    for (size_t i = 0; i < sizeof(methods) / sizeof(*methods); i++)
        if (methods[i].builtin == builtin)
            return &methods[i];
    return NULL;
}

bool is_native_string_method(const builtin_function_t *builtin) {
    return find_method(builtin) != NULL;
}

const builtin_function_t *find_native_string_method(string_view_t name) {
    for (size_t i = 0; i < sizeof(methods) / sizeof(*methods); i++) {
        const builtin_function_t *builtin = methods[i].builtin;
        if (STRLEN(builtin->name) == name.length && !MEMCMP(name.data, builtin->name, name.length))
            return builtin;
    }
    return NULL;
}

static const member_access_t *member_call(const node_t *site) {
    const node_t *callee = replacement_original(get_node_child(site, 0));
    return callee && callee->vtbl->type == NODE_MEMBER_ACCESS ? (const member_access_t *)callee
                                                              : NULL;
}

const builtin_function_t *resolve_native_string_method(const node_t *site,
                                                       const c_expression_context_t *context) {
    const member_access_t *member = member_call(site);
    if (member) {
        const node_t *receiver = replacement_original((const node_t *)member->object);
        for (const c_expression_proof_t *proof = context ? context->head : NULL; proof;
             proof = proof->next)
            if (proof->node == receiver && proof->value_type == C_VALUE_STRING)
                return find_native_string_method(member->name);
    }
    return NULL;
}

/** @brief Logical arguments: receiver first, followed by method operands. */
static const node_t *argument(const node_t *site, size_t index) {
    const member_access_t *member = member_call(site);
    return member ? (index ? get_node_child(site, index) : (const node_t *)member->object)
                  : get_node_child(site, index + 1);
}

bool c_string_method_supported(const node_t *site,
                               const builtin_function_t *builtin,
                               const c_expression_context_t *context) {
    const method_t *method = find_method(builtin);
    size_t count = get_node_child_count(site) - (member_call(site) ? 0 : 1);
    if (!method || count < method->min)
        return false;
    for (size_t i = 0; i < count; i++) {
        c_value_type_t type = c_expression_type(context, replacement_original(argument(site, i)));
        if (!c_type_name(type))
            return false;
        if (!i && type != C_VALUE_STRING)
            return false;
        if (i && i < method->max && method->kind != FORMAT) {
            if (method->integers
                    ? type != C_VALUE_INT64
                    : type != C_VALUE_STRING && !(method->characters && type == C_VALUE_CHAR))
                return false;
        }
    }
    return true;
}

c_generated_expression_t c_string_method_call(const node_t *site,
                                              c_generation_context_t *context,
                                              const builtin_function_t *builtin) {
    const method_t *method = find_method(builtin);
    c_expression_context_t proof = {.head = context->summary->c_expressions};
    if (!context->module_definition || !c_string_method_supported(site, builtin, &proof)
        || c_generation_expression_type(context, site) != method->result) {
        fail_c_generation(context, site, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    size_t count = get_node_child_count(site) - (member_call(site) ? 0 : 1);
    string_value_t *values = CALLOC(count * sizeof(*values));
    c_value_type_t *types = ALLOC(count * sizeof(*types));
    source_builder_t *prelude = create_source_builder();
    bool success = true;
    /* CALLM evaluates explicit operands first, then the receiver. Plain calls are left-to-right. */
    for (size_t step = 0; step < count; step++) {
        size_t i = member_call(site) ? (step + 1) % count : step;
        c_generated_expression_t expr = generate_c_code_from_node(argument(site, i), context);
        if (!expr.success) {
            destroy_c_expression(&expr);
            success = false;
            break;
        }
        types[i] = expr.type;
        values[i] = c_capture_operand(prelude, context, &expr, expr.type);
        destroy_c_expression(&expr);
    }
    string_value_t result = {0};
    if (success) {
        size_t used = count < method->max ? count : method->max;
        for (size_t i = 1; i < count; i++) {
            if (i >= used)
                add_source(prelude, 0, U"(void)%s;", values[i].data);
            else if (method->kind == FORMAT || (method->characters && types[i] == C_VALUE_CHAR)) {
                string_value_t converted =
                    c_string_from_value(prelude, context, types[i], values[i].data);
                FREE_STRING(values[i]);
                values[i] = converted;
            }
        }
        string_builder_t call;
        init_string_builder(&call, 128);
        string_value_t prefix = format_string(U"goat_native_string_%s(", method->name);
        append_string_value(&call, prefix);
        FREE_STRING(prefix);
        if (method->kind != DIRECT && builtin != &builtin_char_at)
            append_string(&call, U"g_context, ");
        append_string_value(&call, values[0]);
        if (method->kind == FORMAT) {
            if (count > 1) {
                string_value_t array = format_string(U"g_t%zu", context->temporary_count++);
                add_source(prelude,
                           0,
                           U"goat_native_string_view_v2_t %s[%zu];",
                           array.data,
                           count - 1);
                for (size_t i = 1; i < count; i++)
                    add_source(prelude, 0, U"%s[%zu] = %s;", array.data, i - 1, values[i].data);
                string_value_t text = format_string(U", %s, %zu", array.data, count - 1);
                append_string_value(&call, text);
                FREE_STRING(text);
                FREE_STRING(array);
            } else
                append_string(&call, U", NULL, 0");
        } else {
            for (size_t i = 1; i < used; i++) {
                append_string(&call, U", ");
                append_string_value(&call, values[i]);
            }
            if (builtin == &builtin_substring && used == 2) {
                string_value_t end =
                    format_string(U", goat_native_string_length(%s)", values[0].data);
                append_string_value(&call, end);
                FREE_STRING(end);
            }
        }
        string_value_t output = format_string(U"g_t%zu", context->temporary_count++);
        bool buffer = method->kind == BUFFER || method->kind == FORMAT;
        if (method->kind == DIRECT) {
            string_value_t text = append_char(&call, U')');
            add_source(prelude,
                       0,
                       U"%s %s = %s;",
                       c_type_name(method->result),
                       output.data,
                       text.data);
            FREE_STRING(text);
        } else {
            add_source(prelude,
                       0,
                       U"%s %s = {0};",
                       buffer ? U"goat_native_string_buffer_v2_t" : c_type_name(method->result),
                       output.data);
            append_string(&call, U", &");
            append_string_value(&call, output);
            string_value_t text = append_char(&call, U')');
            add_source(prelude,
                       0,
                       U"if (%s != GOAT_NATIVE_OK) longjmp(g_guard->recovery, 1);",
                       text.data);
            FREE_STRING(text);
        }
        result = buffer ? format_string(U"goat_native_string_view(%s)", output.data)
                        : format_string(U"%s", output.data);
        FREE_STRING(output);
    }
    for (size_t i = 0; i < count; i++)
        FREE_STRING(values[i]);
    FREE(values);
    FREE(types);
    if (!success) {
        destroy_source_builder(prelude);
        return (c_generated_expression_t){0};
    }
    return (c_generated_expression_t){.success = true,
                                      .type = method->result,
                                      .value = result,
                                      .prelude = prelude};
}

c_generated_expression_t c_string_length(const node_t *node, c_generation_context_t *context) {
    c_generated_expression_t receiver = generate_c_code_from_node(get_node_child(node, 0), context);
    if (!receiver.success || receiver.type != C_VALUE_STRING || !context->module_definition) {
        destroy_c_expression(&receiver);
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    source_builder_t *prelude = create_source_builder();
    string_value_t value = c_capture_operand(prelude, context, &receiver, C_VALUE_STRING);
    string_value_t result = format_string(U"goat_native_string_length(%s)", value.data);
    FREE_STRING(value);
    destroy_c_expression(&receiver);
    return (c_generated_expression_t){.success = true,
                                      .type = C_VALUE_INT64,
                                      .value = result,
                                      .prelude = prelude};
}
