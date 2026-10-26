/** @file c_module_v1.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Checked fixed-width module ABI adapters, sharing existing C bodies and guards.
 */
#include "c_module_v1.h"

#include "analysis/native_builtin.h"
#include "builtins/registry.h"
#include "c_lowering.h"
#include "graph/replacement.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "lib/wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *kind(c_value_type_t type) {
    return type == C_VALUE_INT64    ? "integer"
           : type == C_VALUE_BOOL   ? "boolean"
           : type == C_VALUE_CHAR   ? "character"
           : type == C_VALUE_STRING ? "string"
                                    : "real";
}

static bool emitted(const c_module_output_t *output, const c_module_function_t *entry) {
    for (const c_module_failure_t *failure = output->failures; failure; failure = failure->next)
        if (failure->entry == entry)
            return false;
    return true;
}

static bool scalar(uint32_t value) {
    return value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff);
}

/** The legacy language permits full uint32 characters. Module ABI v1 permits
 * Unicode scalars only; reject unproven construction rather than fail after entry.
 * Character/string inputs are checked by the host before entering this code.
 */
static bool scalar_constructions(const function_summary_t *summary, const node_t *node) {
    node = replacement_original(node);
    if (node != summary->function && node->vtbl->type == NODE_FUNCTION_OBJECT)
        return true;
    if (node->vtbl->type == NODE_FUNCTION_CALL
        && resolve_native_builtin(replacement_original(get_node_child(node, 0))) == &builtin_chr) {
        bool proven = false;
        for (const c_expression_proof_t *proof = summary->c_expressions; proof; proof = proof->next)
            if (proof->node == node && proof->constant
                && proof->constant->type == LATTICE_CHAR_CONSTANT)
                proven = scalar(((const char_constant_element_t *)proof->constant)->value);
        if (!proven)
            return false;
    }
    for (size_t i = 0; i < get_node_child_count(node); ++i)
        if (!scalar_constructions(summary, get_node_child(node, i)))
            return false;
    return true;
}

static bool scalar_constants(const function_summary_t *summary) {
    for (const c_expression_proof_t *proof = summary->c_expressions; proof; proof = proof->next) {
        if (!proof->constant)
            continue;
        if (proof->constant->type == LATTICE_CHAR_CONSTANT
            && !scalar(((const char_constant_element_t *)proof->constant)->value))
            return false;
        if (proof->constant->type == LATTICE_STRING_CONSTANT) {
            string_view_t text = ((const string_constant_element_t *)proof->constant)->value;
            for (size_t i = 0; i < text.length; ++i)
                if (!scalar((uint32_t)text.data[i]))
                    return false;
        }
    }
    return true;
}

static void bridge(source_builder_t *builder) {
    add_static_source(builder, 0, U"#include \"module_abi.h\"");
    add_static_source(builder, 0, U"#include <string.h>");
    add_static_source(builder,
                      0,
                      U"/* The legacy types are internal lowering details, never exported. */");
    add_static_source(builder,
                      0,
                      U"static _Thread_local const goat_module_context_v1 *g_v1_context;");
    add_static_source(builder,
                      0,
                      U"static void *GOAT_NATIVE_CALL g_v1_allocate(void *state, size_t size) {");
    add_static_source(builder, 1, U"const goat_module_context_v1 *context = state;");
    add_static_source(builder,
                      1,
                      U"return context->services->allocate(context->state, (uint64_t)size);");
    add_static_source(builder, 0, U"}");
    add_static_source(
        builder,
        0,
        U"static void GOAT_NATIVE_CALL g_v1_deallocate(void *state, void *pointer) {");
    add_static_source(builder, 1, U"const goat_module_context_v1 *context = state;");
    add_static_source(builder, 1, U"context->services->deallocate(context->state, pointer);");
    add_static_source(builder, 0, U"}");
    add_static_source(builder,
                      0,
                      U"static void GOAT_NATIVE_CALL g_v1_deallocate_all(void *state) {");
    add_static_source(builder, 1, U"const goat_module_context_v1 *context = state;");
    add_static_source(builder, 1, U"context->services->deallocate_all(context->state);");
    add_static_source(builder, 0, U"}");
    add_static_source(builder, 0, U"static int GOAT_NATIVE_CALL g_v1_space(gchar_t value) {");
    add_static_source(builder,
                      1,
                      U"return g_v1_context->services->is_space((uint32_t)value) != 0;");
    add_static_source(builder, 0, U"}");
    add_static_source(builder, 0, U"static gchar_t GOAT_NATIVE_CALL g_v1_upper(gchar_t value) {");
    add_static_source(builder,
                      1,
                      U"return (gchar_t)g_v1_context->services->to_upper((uint32_t)value);");
    add_static_source(builder, 0, U"}");
    add_static_source(builder, 0, U"static gchar_t GOAT_NATIVE_CALL g_v1_lower(gchar_t value) {");
    add_static_source(builder,
                      1,
                      U"return (gchar_t)g_v1_context->services->to_lower((uint32_t)value);");
    add_static_source(builder, 0, U"}");
    add_static_source(builder, 0, U"static const goat_native_services_v2_t g_v1_services = {");
    add_static_source(builder,
                      1,
                      U"sizeof(goat_native_services_v2_t), g_v1_allocate, g_v1_deallocate,");
    add_static_source(builder, 1, U"g_v1_deallocate_all, g_v1_space, g_v1_upper, g_v1_lower");
    add_static_source(builder, 0, U"};");
    add_static_source(
        builder,
        0,
        U"static uint32_t g_v1_invoke(goat_native_adapter_v2_t adapter, uint32_t version,");
    add_static_source(builder, 1, U"const goat_module_context_v1 *context, uint32_t count,");
    add_static_source(builder,
                      1,
                      U"const goat_module_value_v1 *args, goat_module_value_v1 *result) {");
    add_static_source(builder,
                      1,
                      U"if (version != GOAT_MODULE_ABI_VERSION) return GOAT_MODULE_ABI_MISMATCH;");
    add_static_source(builder,
                      1,
                      U"if (!result || !context || !context->services || (count && !args)) return "
                      U"GOAT_MODULE_BAD_REQUEST;");
    add_static_source(builder, 1, U"const goat_module_services_v1 *services = context->services;");
    add_static_source(
        builder,
        1,
        U"if (services->struct_size < GOAT_MODULE_SERVICES_SIZE || services->reserved ||");
    add_static_source(
        builder,
        2,
        U"!services->allocate || !services->deallocate || !services->deallocate_all ||");
    add_static_source(builder,
                      2,
                      U"!services->is_space || !services->to_upper || !services->to_lower) return "
                      U"GOAT_MODULE_BAD_REQUEST;");
    add_static_source(builder, 1, U"*result = (goat_module_value_v1){0};");
    add_static_source(builder,
                      1,
                      U"if ((uint64_t)count * sizeof(goat_native_value_v2_t) > SIZE_MAX ||");
    add_static_source(builder,
                      2,
                      U"(uint64_t)count * sizeof(goat_native_string_view_v2_t) > SIZE_MAX) return "
                      U"GOAT_MODULE_BAD_REQUEST;");
    add_static_source(
        builder,
        1,
        U"goat_native_value_v2_t *values = count ? services->allocate(context->state, "
        U"(uint64_t)count * sizeof(*values)) : 0;");
    add_static_source(
        builder,
        1,
        U"goat_native_string_view_v2_t *strings = count ? services->allocate(context->state, "
        U"(uint64_t)count * sizeof(*strings)) : 0;");
    add_static_source(builder, 1, U"for (uint32_t i = 0; i < count; ++i) {");
    add_static_source(builder,
                      2,
                      U"values[i] = (goat_native_value_v2_t){0}; values[i].type = args[i].tag;");
    add_static_source(builder, 2, U"if (args[i].reserved) return GOAT_MODULE_BAD_REQUEST;");
    add_static_source(builder, 2, U"switch (args[i].tag) {");
    add_static_source(
        builder,
        3,
        U"case GOAT_MODULE_NULL: if (args[i].payload) return GOAT_MODULE_BAD_REQUEST; break;");
    add_static_source(
        builder,
        3,
        U"case GOAT_MODULE_INTEGER: memcpy(&values[i].value.integer, &args[i].payload, 8); break;");
    add_static_source(
        builder,
        3,
        U"case GOAT_MODULE_REAL: memcpy(&values[i].value.real, &args[i].payload, 8); break;");
    add_static_source(
        builder,
        3,
        U"case GOAT_MODULE_BOOLEAN: if (args[i].payload > 1) return GOAT_MODULE_BAD_REQUEST; "
        U"values[i].value.boolean = (uint32_t)args[i].payload; break;");
    add_static_source(
        builder,
        3,
        U"case GOAT_MODULE_CHARACTER: if (args[i].payload > 0x10ffff || "
        U"(args[i].payload >= 0xd800 && args[i].payload <= 0xdfff)) return "
        U"GOAT_MODULE_BAD_REQUEST; values[i].value.character = (uint32_t)args[i].payload; break;");
    add_static_source(builder, 3, U"case GOAT_MODULE_STRING: {");
    add_static_source(builder,
                      4,
                      U"if (!args[i].payload || args[i].payload > UINTPTR_MAX || args[i].payload % "
                      U"8) return GOAT_MODULE_BAD_REQUEST;");
    add_static_source(
        builder,
        4,
        U"const goat_module_string_v1 *view = (const void *)(uintptr_t)args[i].payload;");
    add_static_source(builder,
                      4,
                      U"if ((!view->data && view->length) || view->length > "
                      U"GOAT_NATIVE_STRING_MAX) return GOAT_MODULE_BAD_REQUEST;");
    add_static_source(builder, 0, U"#if UINTPTR_MAX == UINT32_MAX");
    add_static_source(builder, 4, U"if (view->reserved) return GOAT_MODULE_BAD_REQUEST;");
    add_static_source(builder, 0, U"#endif");
    add_static_source(builder,
                      4,
                      U"if (view->length && (uintptr_t)view->data % _Alignof(uint32_t)) return "
                      U"GOAT_MODULE_BAD_REQUEST;");
    add_static_source(
        builder,
        4,
        U"for (size_t j = 0; j < view->length; ++j) if (view->data[j] > 0x10ffff || (view->data[j] "
        U">= 0xd800 && view->data[j] <= 0xdfff)) return GOAT_MODULE_BAD_REQUEST;");
    add_static_source(builder,
                      4,
                      U"strings[i] = (goat_native_string_view_v2_t){(const gchar_t *)view->data, "
                      U"(size_t)view->length};");
    add_static_source(builder, 4, U"values[i].value.string = strings + i; break;");
    add_static_source(builder, 3, U"}");
    add_static_source(builder, 3, U"default: return GOAT_MODULE_BAD_REQUEST;");
    add_static_source(builder, 2, U"}");
    add_static_source(builder, 1, U"}");
    add_static_source(builder,
                      1,
                      U"goat_native_context_v2_t lowered = {&g_v1_services, (void *)context};");
    add_static_source(
        builder,
        1,
        U"const goat_module_context_v1 *previous = g_v1_context; g_v1_context = context;");
    add_static_source(builder, 1, U"goat_native_value_v2_t value = {0};");
    add_static_source(
        builder,
        1,
        U"uint32_t status = adapter(GOAT_NATIVE_ABI_VERSION, &lowered, count, values, &value);");
    add_static_source(builder, 1, U"g_v1_context = previous;");
    add_static_source(builder,
                      1,
                      U"services->deallocate(context->state, values); "
                      U"services->deallocate(context->state, strings);");
    add_static_source(builder, 1, U"if (status) return status;");
    add_static_source(builder, 1, U"result->tag = value.type;");
    add_static_source(builder, 1, U"switch (value.type) {");
    add_static_source(
        builder,
        2,
        U"case GOAT_NATIVE_I64: memcpy(&result->payload, &value.value.integer, 8); break;");
    add_static_source(
        builder,
        2,
        U"case GOAT_NATIVE_F64: memcpy(&result->payload, &value.value.real, 8); break;");
    add_static_source(builder,
                      2,
                      U"case GOAT_NATIVE_BOOL: result->payload = value.value.boolean; break;");
    add_static_source(builder,
                      2,
                      U"case GOAT_NATIVE_CHAR: result->payload = value.value.character; break;");
    add_static_source(builder, 2, U"case GOAT_NATIVE_STRING: {");
    add_static_source(
        builder,
        3,
        U"goat_module_string_v1 *view = services->allocate(context->state, sizeof(*view));");
    add_static_source(builder,
                      3,
                      U"*view = (goat_module_string_v1){0}; view->data = (const uint32_t "
                      U"*)value.value.string->data;");
    add_static_source(
        builder,
        3,
        U"view->length = value.value.string->length; result->payload = (uintptr_t)view; break;");
    add_static_source(builder, 2, U"}");
    add_static_source(
        builder,
        2,
        U"default: *result = (goat_module_value_v1){0}; return GOAT_MODULE_EXTERNAL_ERROR;");
    add_static_source(builder, 1, U"}");
    add_static_source(builder, 1, U"return GOAT_MODULE_SUCCESS;");
    add_static_source(builder, 0, U"}");
}

c_module_v1_output_t
generate_c_module_v1(arena_t *arena, const node_t *const *roots, size_t count) {
    c_module_t *module = create_c_module_roots(arena, roots, count);
    for (c_module_function_t *entry = module->head; entry; entry = entry->next)
        if (!scalar_constructions(entry->summary, entry->summary->function)
            || !scalar_constants(entry->summary))
            entry->available = false;
    c_module_output_t lowered = generate_c_module_implementation(arena, module);
    c_module_v1_output_t output = {.generated_count = lowered.generated_count,
                                   .omitted_count = lowered.omitted_count};
    source_builder_t *builder = create_source_builder();
    add_formatted_source(builder, 0, lowered.source);
    bridge(builder);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *specs = yyjson_mut_arr(doc);
    yyjson_mut_doc_set_root(doc, specs);
    for (const c_module_function_t *entry = module->head; entry; entry = entry->next) {
        if (!emitted(&lowered, entry))
            continue;
        c_generation_context_t context = {.summary = entry->summary};
        size_t id = entry->id + 1;
        char symbol[64];
        snprintf(symbol, sizeof(symbol), "goat_fn_%zu_v1", id);
        yyjson_mut_val *spec = yyjson_mut_obj(doc), *signature = yyjson_mut_obj(doc),
                       *when = yyjson_mut_obj(doc), *types = yyjson_mut_arr(doc);
        yyjson_mut_obj_add_uint(doc, spec, "specializationId", id);
        yyjson_mut_obj_add_uint(doc, spec, "functionId", entry->function_id);
        yyjson_mut_obj_add_strcpy(doc, spec, "symbol", symbol);
        yyjson_mut_obj_add_str(doc, signature, "id", "goat.adapter.v1");
        for (size_t i = 0; i < entry->summary->parameter_count; ++i)
            yyjson_mut_arr_add_str(doc, types, kind(c_generation_parameter_type(&context, i)));
        yyjson_mut_obj_add_val(doc, signature, "arguments", types);
        yyjson_mut_obj_add_str(doc, signature, "result", kind(c_generation_return_type(&context)));
        yyjson_mut_obj_add_val(doc, when, "argumentTypes", yyjson_mut_val_mut_copy(doc, types));
        yyjson_mut_obj_add_val(doc, spec, "signature", signature);
        yyjson_mut_obj_add_val(doc, spec, "when", when);
        yyjson_mut_arr_append(specs, spec);
        add_source(builder,
                   0,
                   U"GOAT_MODULE_EXPORT uint32_t GOAT_MODULE_CALL goat_fn_%zu_v1(uint32_t version,",
                   id);
        add_static_source(builder, 1, U"const goat_module_context_v1 *context, uint32_t count,");
        add_static_source(
            builder,
            1,
            U"const goat_module_value_v1 *arguments, goat_module_value_v1 *result) {");
        add_source(
            builder,
            1,
            U"return g_v1_invoke(g_adapter_%zu, version, context, count, arguments, result);",
            entry->id);
        add_static_source(builder, 0, U"}");
    }
    output.specializations = yyjson_mut_doc_imut_copy(doc, NULL);
    yyjson_mut_doc_free(doc);
    output.source = build_source(builder);
    destroy_source_builder(builder);
    return output;
}

void destroy_c_module_v1_output(c_module_v1_output_t *output) {
    if (!output)
        return;
    FREE_STRING(output->source);
    yyjson_doc_free(output->specializations);
    *output = (c_module_v1_output_t){0};
}

string_value_t append_c_module_v1_descriptor(string_view_t source,
                                             const yyjson_doc *metadata,
                                             const yyjson_val *target,
                                             const yyjson_doc *specializations) {
    if (!metadata || !target || !specializations)
        return (string_value_t){0};
    yyjson_val *main = yyjson_doc_get_root((yyjson_doc *)metadata);
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_uint(doc, root, "formatVersion", 1);
    yyjson_mut_obj_add_uint(doc, root, "goatAbiVersion", 1);
    yyjson_mut_obj_add_val(doc,
                           root,
                           "module",
                           yyjson_val_mut_copy(doc, yyjson_obj_get(main, "name")));
    yyjson_mut_obj_add_val(doc,
                           root,
                           "version",
                           yyjson_val_mut_copy(doc, yyjson_obj_get(main, "version")));
    yyjson_mut_obj_add_val(doc,
                           root,
                           "mainHash",
                           yyjson_val_mut_copy(doc, yyjson_obj_get(main, "mainHash")));
    yyjson_mut_obj_add_val(doc, root, "target", yyjson_val_mut_copy(doc, target));
    yyjson_mut_obj_add_val(
        doc,
        root,
        "specializations",
        yyjson_val_mut_copy(doc, yyjson_doc_get_root((yyjson_doc *)specializations)));
    yyjson_doc *input = yyjson_mut_doc_imut_copy(doc, NULL);
    char *json = NULL;
    size_t length = 0;
    goat_module_json_error error =
        goat_module_json_write(input, GOAT_MODULE_NATIVE, false, &json, &length);
    yyjson_doc_free(input);
    yyjson_mut_doc_free(doc);
    if (error)
        return (string_value_t){0};
    source_builder_t *builder = create_source_builder();
    add_source(builder, 0, U"%v", source);
    add_static_source(builder,
                      0,
                      U"GOAT_MODULE_EXPORT const unsigned char goat_module_descriptor_v1[] = {");
    const unsigned char magic[] = "GOATND01";
    unsigned char size[8];
    write_le32(size, (uint32_t)length);
    write_le32(size + 4, (uint32_t)((uint64_t)length >> 32));
    for (size_t i = 0; i < 8; ++i)
        add_source(builder, 1, U"%u,", (unsigned)magic[i]);
    for (size_t i = 0; i < 8; ++i)
        add_source(builder, 1, U"%u,", (unsigned)size[i]);
    for (size_t i = 0; i < length; ++i)
        add_source(builder, 1, U"%u,", (unsigned char)json[i]);
    add_static_source(builder, 1, U"0");
    add_static_source(builder, 0, U"};");
    string_value_t out = build_source(builder);
    destroy_source_builder(builder);
    goat_module_json_free(json);
    return out;
}
