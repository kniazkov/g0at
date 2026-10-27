/** @file c_adapter.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Checked numeric adapters and library-owned immutable metadata.
 */
#include "c_adapter.h"

#include "c_lowering.h"
#include "lib/memory/allocate.h"
#include "lib/strings/string_ext.h"

static const gchar_t *type_tag(c_value_type_t type) {
    return type == C_VALUE_INT64    ? U"GOAT_NATIVE_I64"
           : type == C_VALUE_BOOL   ? U"GOAT_NATIVE_BOOL"
           : type == C_VALUE_CHAR   ? U"GOAT_NATIVE_CHAR"
           : type == C_VALUE_STRING ? U"GOAT_NATIVE_STRING"
                                    : U"GOAT_NATIVE_F64";
}

static const gchar_t *value_field(c_value_type_t type) {
    return type == C_VALUE_BOOL     ? U"boolean"
           : type == C_VALUE_CHAR   ? U"character"
           : type == C_VALUE_INT64  ? U"integer"
           : type == C_VALUE_STRING ? U"string"
                                    : U"real";
}

void c_emit_adapter(source_builder_t *builder, const c_module_function_t *entry) {
    c_generation_context_t context = {.summary = entry->summary};
    size_t count = entry->summary->parameter_count;
    add_static_source(builder, 0, U"");
    if (count) {
        add_source(builder, 0, U"static const uint32_t g_params_%zu[] = {", entry->id);
        for (size_t i = 0; i < count; i++)
            add_source(builder, 1, U"%s,", type_tag(c_generation_parameter_type(&context, i)));
        add_static_source(builder, 0, U"};");
    }
    add_static_source(builder, 0, U"");
    add_source(builder, 0, U"/* Native ABI adapter for %s. */", entry->name.data);
    add_source(builder,
               0,
               U"static uint32_t GOAT_NATIVE_CALL g_adapter_%zu(uint32_t version, const "
               U"goat_native_context_v2_t *g_context, uint32_t count,",
               entry->id);
    add_static_source(builder,
                      1,
                      U"const goat_native_value_v2_t *args, goat_native_value_v2_t *result) {");
    add_static_source(builder,
                      1,
                      U"if (version != GOAT_NATIVE_ABI_VERSION) return GOAT_NATIVE_ABI_MISMATCH;");
    add_static_source(builder,
                      1,
                      U"if (!result || (count && !args)) return GOAT_NATIVE_BAD_REQUEST;");
    add_static_source(
        builder,
        1,
        U"if (!goat_native_has_memory_services(g_context)) return GOAT_NATIVE_BAD_REQUEST;");
    if (count) {
        add_source(builder, 1, U"if (count < %zu) return GOAT_NATIVE_TYPE_MISMATCH;", count);
        for (size_t i = 0; i < count; i++) {
            add_source(builder, 1, U"if (args[%zu].reserved) return GOAT_NATIVE_BAD_REQUEST;", i);
            add_source(builder,
                       1,
                       U"if (args[%zu].type != %s) return GOAT_NATIVE_TYPE_MISMATCH;",
                       i,
                       type_tag(c_generation_parameter_type(&context, i)));
            if (c_generation_parameter_type(&context, i) == C_VALUE_BOOL)
                add_source(builder,
                           1,
                           U"if (args[%zu].value.boolean > 1) return GOAT_NATIVE_BAD_REQUEST;",
                           i);
            if (c_generation_parameter_type(&context, i) == C_VALUE_STRING) {
                add_source(builder,
                           1,
                           U"if (!args[%zu].value.string || "
                           U"(!args[%zu].value.string->data && args[%zu].value.string->length) || "
                           U"args[%zu].value.string->length > GOAT_NATIVE_STRING_MAX) "
                           U"return GOAT_NATIVE_BAD_REQUEST;",
                           i,
                           i,
                           i,
                           i);
            }
        }
    }
    add_static_source(builder, 1, U"g_native_guard_t guard = {.depth = 0};");
    add_static_source(builder, 1, U"guard.origin = (uintptr_t)&guard;");
    add_static_source(builder, 1, U"g_native_guard_t *previous = g_guard;");
    add_static_source(builder, 1, U"if (setjmp(guard.recovery)) {");
    add_static_source(builder, 2, U"g_guard = previous;");
    add_static_source(builder, 2, U"return GOAT_NATIVE_RESOURCE_LIMIT;");
    add_static_source(builder, 1, U"}");
    add_static_source(builder, 1, U"g_guard = &guard;");
    add_static_source(builder, 1, U"goat_native_value_v2_t value = {0};");
    add_source(builder, 1, U"value.type = %s;", type_tag(c_generation_return_type(&context)));
    string_builder_t call;
    init_string_builder(&call, 64);
    append_substring(&call, entry->name.data, entry->name.length);
    append_string(&call, U"(g_context");
    for (size_t i = 0; i < count; i++) {
        append_string(&call, U", ");
        string_value_t argument =
            format_string(U"%sargs[%zu].value.%s",
                          c_generation_parameter_type(&context, i) == C_VALUE_STRING ? U"*" : U"",
                          i,
                          value_field(c_generation_parameter_type(&context, i)));
        append_string_value(&call, argument);
        FREE_STRING(argument);
    }
    string_value_t expression = append_char(&call, U')');
    if (c_generation_return_type(&context) == C_VALUE_STRING) {
        add_source(builder, 1, U"goat_native_string_view_v2_t string_value = %s;", expression.data);
        add_static_source(builder,
                          1,
                          U"goat_native_string_view_v2_t *string_result = "
                          U"goat_native_allocate(g_context, sizeof(*string_result));");
        add_static_source(builder, 1, U"*string_result = string_value;");
        add_static_source(builder, 1, U"value.value.string = string_result;");
    } else {
        add_source(builder,
                   1,
                   U"value.value.%s = %s;",
                   value_field(c_generation_return_type(&context)),
                   expression.data);
    }
    FREE_STRING(expression);
    add_static_source(builder, 1, U"g_guard = previous;");
    add_static_source(builder, 1, U"*result = value;");
    add_static_source(builder, 1, U"return GOAT_NATIVE_OK;");
    add_static_source(builder, 0, U"}");
}

void c_emit_native_module(source_builder_t *builder,
                          const c_module_t *module,
                          const c_generation_result_t *results,
                          size_t count) {
    add_static_source(builder, 0, U"");
    if (count) {
        add_static_source(builder,
                          0,
                          U"static const goat_native_entry_v2_t g_native_entries[] = {");
        for (const c_module_function_t *entry = module->head; entry; entry = entry->next) {
            if (results[entry->id].status != C_GENERATION_OK)
                continue;
            c_generation_context_t context = {.summary = entry->summary};
            string_value_t parameters = entry->summary->parameter_count
                                            ? format_string(U"g_params_%zu", entry->id)
                                            : STATIC_STRING(U"0");
            add_source(builder,
                       1,
                       U"{%zu, %zu, %zu, %s, %s, g_adapter_%zu, 0, GOAT_NATIVE_PURE},",
                       entry->id,
                       entry->function_id,
                       entry->summary->parameter_count,
                       type_tag(c_generation_return_type(&context)),
                       parameters.data,
                       entry->id);
            FREE_STRING(parameters);
        }
        add_static_source(builder, 0, U"};");
    }
    add_static_source(builder, 0, U"");
    add_static_source(builder, 0, U"static const goat_native_module_v2_t g_native_module = {");
    add_static_source(builder, 1, U"GOAT_NATIVE_ABI_VERSION, sizeof(goat_native_module_v2_t),");
    add_static_source(builder,
                      1,
                      U"sizeof(goat_native_value_v2_t), _Alignof(goat_native_value_v2_t),");
    add_static_source(builder, 1, U"sizeof(goat_native_entry_v2_t), sizeof(void *),");
    add_source(builder, 1, U"%zu, %s", count, count ? U"g_native_entries" : U"0");
    add_static_source(builder, 0, U"};");
    add_static_source(builder, 0, U"");
    add_static_source(builder,
                      0,
                      U"GOAT_NATIVE_API const goat_native_module_v2_t *GOAT_NATIVE_CALL "
                      U"goat_native_query_v2(uint32_t version) {");
    add_static_source(builder,
                      1,
                      U"return version == GOAT_NATIVE_ABI_VERSION ? &g_native_module : 0;");
    add_static_source(builder, 0, U"}");
}
