/** @file c_call.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exact call-site proofs and explicitly sequenced native arguments.
 */
#include "c_call.h"

#include "analysis/function_call_graph.h"
#include "analysis/native_builtin.h"
#include "c_arithmetic.h"
#include "c_lowering.h"
#include "graph/replacement.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

#include <wchar.h>

static bool same_signature(const function_summary_t *a, const function_summary_t *b) {
    if (!a || !b || a->function != b->function || a->parameter_count != b->parameter_count)
        return false;
    for (size_t i = 0; i < a->parameter_count; i++) {
        if (!a->parameter_types || !b->parameter_types || !a->parameter_types[i]
            || !b->parameter_types[i] || a->parameter_types[i]->type != b->parameter_types[i]->type)
            return false;
    }
    return true;
}

static bool same_name(string_view_t a, string_view_t b) {
    return a.length == b.length && !wmemcmp(a.data, b.data, a.length);
}

static bool usable(const function_summary_t *summary) {
    if (!summary || !summary->function || summary->function->vtbl->type != NODE_FUNCTION_OBJECT
        || summary->status != FUNCTION_ANALYZED || summary->c_support != FUNCTION_C_SUPPORTED
        || summary->c_blockers || !function_summary_is_pure(summary)
        || get_node_child_count(get_node_child(summary->function, 0)) != summary->parameter_count)
        return false;
    c_generation_context_t context = {.summary = summary};
    c_value_type_t type = c_generation_return_type(&context);
    if (type != C_VALUE_INT64 && type != C_VALUE_DOUBLE)
        return false;
    for (size_t i = 0; i < summary->parameter_count; i++) {
        type = c_generation_parameter_type(&context, i);
        if (type != C_VALUE_INT64 && type != C_VALUE_DOUBLE)
            return false;
    }
    return true;
}

bool c_emit_callee_prototypes(c_generation_context_t *context, source_builder_t *builder) {
    for (const c_generation_callee_t *callee = context->callees; callee; callee = callee->next) {
        if (!usable(callee->summary))
            return fail_c_generation(context, context->summary->function, C_GENERATION_NOT_PROVEN);
        if (!c_function_name_is_valid(callee->name))
            return fail_c_generation(context,
                                     context->summary->function,
                                     C_GENERATION_INVALID_REQUEST);
        bool self = same_signature(callee->summary, context->summary);
        if (self != same_name(callee->name, context->function_name)
            || (self && callee->summary->return_type->type != context->summary->return_type->type))
            return fail_c_generation(context,
                                     context->summary->function,
                                     C_GENERATION_INVALID_REQUEST);
        bool duplicate = false;
        for (const c_generation_callee_t *old = context->callees; old != callee; old = old->next) {
            bool same = same_signature(old->summary, callee->summary);
            if (same != same_name(old->name, callee->name)
                || (same && old->summary->return_type->type != callee->summary->return_type->type))
                return fail_c_generation(context,
                                         context->summary->function,
                                         C_GENERATION_INVALID_REQUEST);
            duplicate |= same;
        }
        if (duplicate)
            continue;
        if (builder)
            c_emit_prototype(callee->summary, callee->name, builder);
    }
    return true;
}

/** @brief Uses the selected caller's generic call records, never observed argument values. */
static const c_generation_callee_t *resolve(const node_t *node, c_generation_context_t *context) {
    const function_summary_t *target = NULL;
    for (const c_call_t *call = context->summary->c_calls; call; call = call->next) {
        if (call->site != node)
            continue;
        if (!usable(call->target)
            || (target
                && (!same_signature(target, call->target)
                    || target->return_type->type != call->target->return_type->type))) {
            fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
            return NULL;
        }
        target = call->target;
    }
    if (!target
        || resolve_immutable_function(replacement_original(get_node_child(node, 0)))
               != target->function) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return NULL;
    }
    for (const c_generation_callee_t *callee = context->callees; callee; callee = callee->next) {
        if (same_signature(callee->summary, target)) {
            if (callee->summary->return_type->type != target->return_type->type) {
                fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
                return NULL;
            }
            return callee;
        }
    }
    fail_c_generation(context, node, C_GENERATION_UNSUPPORTED);
    return NULL;
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

c_generated_expression_t c_call(const node_t *node, c_generation_context_t *context) {
    if (resolve_native_abs(replacement_original(get_node_child(node, 0))))
        return native_abs(node, context);
    const c_generation_callee_t *callee = resolve(node, context);
    if (!callee)
        return (c_generated_expression_t){0};
    c_generation_context_t target = {.summary = callee->summary};
    c_value_type_t type = c_generation_return_type(&target);
    size_t count = get_node_child_count(node) - 1;
    size_t parameters = callee->summary->parameter_count;
    if (count < parameters || c_generation_expression_type(context, node) != type) {
        fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        return (c_generated_expression_t){0};
    }
    string_value_t *arguments = count ? CALLOC(count * sizeof(*arguments)) : NULL;
    source_builder_t *prelude = create_source_builder();
    bool success = true;
    for (size_t i = count; i > 0 && success;) {
        i--;
        c_generated_expression_t argument =
            generate_c_code_from_node(get_node_child(node, i + 1), context);
        success = argument.success;
        if (success
            && (!c_type_name(argument.type)
                || (i < parameters && argument.type != c_generation_parameter_type(&target, i)))) {
            success = fail_c_generation(context, node, C_GENERATION_NOT_PROVEN);
        }
        if (success) {
            arguments[i] = c_capture_operand(prelude, context, &argument, argument.type);
            if (i >= parameters)
                add_source(prelude, 0, L"(void)%s;", arguments[i].data);
        }
        destroy_c_expression(&argument);
    }
    string_value_t value = {0};
    if (success) {
        string_builder_t call;
        init_string_builder(&call, 64);
        append_substring(&call, callee->name.data, callee->name.length);
        append_char(&call, L'(');
        for (size_t i = 0; i < parameters; i++) {
            if (i)
                append_string(&call, L", ");
            append_string_value(&call, arguments[i]);
        }
        string_value_t expression = append_char(&call, L')');
        value = format_string(L"g_t%zu", context->temporary_count++);
        add_source(prelude,
                   0,
                   L"%s%s %s = %s;",
                   type == C_VALUE_DOUBLE ? L"volatile " : L"",
                   c_type_name(type),
                   value.data,
                   expression.data);
        FREE_STRING(expression);
    }
    for (size_t i = 0; i < count; i++)
        FREE_STRING(arguments[i]);
    FREE(arguments);
    if (!success) {
        destroy_source_builder(prelude);
        return (c_generated_expression_t){0};
    }
    return (c_generated_expression_t){.success = true,
                                      .type = type,
                                      .value = value,
                                      .prelude = prelude};
}

void c_emit_prototype(const function_summary_t *summary,
                      string_view_t name,
                      source_builder_t *builder) {
    c_generation_context_t target = {.summary = summary};
    string_builder_t declaration;
    init_string_builder(&declaration, 64);
    append_string(&declaration, c_type_name(c_generation_return_type(&target)));
    append_char(&declaration, L' ');
    append_substring(&declaration, name.data, name.length);
    append_char(&declaration, L'(');
    if (!summary->parameter_count)
        append_string(&declaration, L"void");
    for (size_t i = 0; i < summary->parameter_count; i++) {
        if (i)
            append_string(&declaration, L", ");
        append_string(&declaration, c_type_name(c_generation_parameter_type(&target, i)));
    }
    add_formatted_source(builder, 0, append_string(&declaration, L");"));
}
