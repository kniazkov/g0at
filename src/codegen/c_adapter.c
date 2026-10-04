/** @file c_adapter.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Checked numeric adapters and library-owned immutable metadata.
 */
#include "c_adapter.h"

#include "c_lowering.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"

/** @brief Kept in sync with native_abi.h by the ABI integration test. */
void c_emit_native_abi(source_builder_t *builder) {
    add_static_source(builder, 0, L"/* ABI declarations begin. */");
    add_static_source(builder, 0, L"#ifndef GOAT_NATIVE_ABI_V1_H");
    add_static_source(builder, 0, L"#define GOAT_NATIVE_ABI_V1_H");
    add_static_source(builder, 0, L"#include <stdint.h>");
    add_static_source(builder, 0, L"");
    add_static_source(builder, 0, L"#ifdef _WIN32");
    add_static_source(builder, 0, L"#define GOAT_NATIVE_CALL __cdecl");
    add_static_source(builder, 0, L"#ifdef GOAT_NATIVE_BUILD");
    add_static_source(builder, 0, L"#define GOAT_NATIVE_API __declspec(dllexport)");
    add_static_source(builder, 0, L"#else");
    add_static_source(builder, 0, L"#define GOAT_NATIVE_API");
    add_static_source(builder, 0, L"#endif");
    add_static_source(builder, 0, L"#else");
    add_static_source(builder, 0, L"#define GOAT_NATIVE_CALL");
    add_static_source(builder, 0, L"#define GOAT_NATIVE_API");
    add_static_source(builder, 0, L"#endif");
    add_static_source(builder, 0, L"");
    add_static_source(builder, 0, L"enum {");
    add_static_source(builder, 0, L"    GOAT_NATIVE_ABI_VERSION = 1,");
    add_static_source(builder, 0, L"    GOAT_NATIVE_INVALID = 0,");
    add_static_source(builder, 0, L"    GOAT_NATIVE_I64 = 1,");
    add_static_source(builder, 0, L"    GOAT_NATIVE_F64 = 2");
    add_static_source(builder, 0, L"};");
    add_static_source(builder, 0, L"");
    add_static_source(builder, 0, L"enum {");
    add_static_source(builder, 0, L"    GOAT_NATIVE_OK = 0,");
    add_static_source(builder, 0, L"    GOAT_NATIVE_TYPE_MISMATCH = 1,");
    add_static_source(builder, 0, L"    GOAT_NATIVE_BAD_REQUEST = 2,");
    add_static_source(builder, 0, L"    GOAT_NATIVE_ABI_MISMATCH = 3,");
    add_static_source(builder, 0, L"    GOAT_NATIVE_RESOURCE_LIMIT = 4,");
    add_static_source(builder, 0, L"    GOAT_NATIVE_EXTERNAL_ERROR = 5");
    add_static_source(builder, 0, L"};");
    add_static_source(builder, 0, L"");
    add_static_source(builder, 0, L"enum { GOAT_NATIVE_PURE = 1 };");
    add_static_source(builder, 0, L"");
    add_static_source(builder, 0, L"#ifdef __cplusplus");
    add_static_source(builder, 0, L"extern \"C\" {");
    add_static_source(builder, 0, L"#endif");
    add_static_source(builder, 0, L"");
    add_static_source(builder, 0, L"typedef struct goat_native_value_v1_t {");
    add_static_source(builder, 0, L"    uint32_t type;");
    add_static_source(builder, 0, L"    uint32_t reserved;");
    add_static_source(builder, 0, L"");
    add_static_source(builder, 0, L"    union {");
    add_static_source(builder, 0, L"        int64_t integer;");
    add_static_source(builder, 0, L"        double real;");
    add_static_source(builder, 0, L"    } value;");
    add_static_source(builder, 0, L"} goat_native_value_v1_t;");
    add_static_source(builder, 0, L"");
    add_static_source(
        builder,
        0,
        L"typedef uint32_t (GOAT_NATIVE_CALL *goat_native_adapter_v1_t)(uint32_t version,");
    add_static_source(builder,
                      0,
                      L"                                             uint32_t argument_count,");
    add_static_source(
        builder,
        0,
        L"                                             const goat_native_value_v1_t *arguments,");
    add_static_source(
        builder,
        0,
        L"                                             goat_native_value_v1_t *result);");
    add_static_source(builder, 0, L"");
    add_static_source(builder, 0, L"typedef struct goat_native_entry_v1_t {");
    add_static_source(builder, 0, L"    uint64_t specialization_id;");
    add_static_source(builder, 0, L"    uint64_t function_id;");
    add_static_source(builder, 0, L"    uint32_t parameter_count;");
    add_static_source(builder, 0, L"    uint32_t return_type;");
    add_static_source(builder, 0, L"    const uint32_t *parameter_types;");
    add_static_source(builder, 0, L"    goat_native_adapter_v1_t invoke;");
    add_static_source(builder, 0, L"    const char *binding_name;");
    add_static_source(builder, 0, L"    uint32_t flags;");
    add_static_source(builder, 0, L"} goat_native_entry_v1_t;");
    add_static_source(builder, 0, L"");
    add_static_source(builder, 0, L"typedef struct goat_native_module_v1_t {");
    add_static_source(builder, 0, L"    uint32_t abi_version;");
    add_static_source(builder, 0, L"    uint32_t struct_size;");
    add_static_source(builder, 0, L"    uint32_t value_size;");
    add_static_source(builder, 0, L"    uint32_t value_alignment;");
    add_static_source(builder, 0, L"    uint32_t entry_size;");
    add_static_source(builder, 0, L"    uint32_t pointer_size;");
    add_static_source(builder, 0, L"    uint32_t entry_count;");
    add_static_source(builder, 0, L"    const goat_native_entry_v1_t *entries;");
    add_static_source(builder, 0, L"} goat_native_module_v1_t;");
    add_static_source(builder, 0, L"");
    add_static_source(builder,
                      0,
                      L"typedef const goat_native_module_v1_t *(GOAT_NATIVE_CALL "
                      L"*goat_native_query_v1_t)(uint32_t version);");
    add_static_source(builder,
                      0,
                      L"GOAT_NATIVE_API const goat_native_module_v1_t *GOAT_NATIVE_CALL "
                      L"goat_native_query_v1(uint32_t version);");
    add_static_source(builder, 0, L"#ifdef __cplusplus");
    add_static_source(builder, 0, L"}");
    add_static_source(builder, 0, L"#endif");
    add_static_source(builder, 0, L"#endif");
    add_static_source(builder, 0, L"/* ABI declarations end. */");
}

void c_emit_native_guard(source_builder_t *builder) {
    static const wchar_t *const lines[] = {
        L"#include <setjmp.h>",
        L"#if !defined(__GNUC__) && !defined(__clang__)",
        L"#error Bounded Goat functions require GCC or Clang noinline support",
        L"#endif",
        L"typedef struct {",
        L"    jmp_buf recovery;",
        L"    unsigned depth;",
        L"    unsigned fuel;",
        L"    uintptr_t origin;",
        L"} goat_native_guard_t;",
        L"static _Thread_local goat_native_guard_t *goat_guard;",
        L"static inline void goat_guard_enter(void) {",
        L"    if (!goat_guard) return;",
        L"    char position;",
        L"    uintptr_t here = (uintptr_t)&position;",
        L"    uintptr_t distance = here > goat_guard->origin ? here - goat_guard->origin : "
        L"goat_guard->origin - here;",
        L"    if (goat_guard->depth >= 32 || !goat_guard->fuel || distance >= 65536)",
        L"        longjmp(goat_guard->recovery, 1);",
        L"    goat_guard->depth++;",
        L"    goat_guard->fuel--;",
        L"}",
        L"static inline void goat_guard_step(void) {",
        L"    if (!goat_guard) return;",
        L"    if (!goat_guard->fuel) longjmp(goat_guard->recovery, 1);",
        L"    goat_guard->fuel--;",
        L"}",
        L"static inline void goat_guard_leave(void) {",
        L"    if (goat_guard) goat_guard->depth--;",
        L"}"};
    for (size_t i = 0; i < sizeof(lines) / sizeof(*lines); i++)
        add_formatted_source(builder, 0, (string_value_t){lines[i], wcslen(lines[i]), false});
}

static const wchar_t *type_tag(c_value_type_t type) {
    return type == C_VALUE_INT64 ? L"GOAT_NATIVE_I64" : L"GOAT_NATIVE_F64";
}

void c_emit_adapter(source_builder_t *builder, const c_module_function_t *entry) {
    c_generation_context_t context = {.summary = entry->summary};
    size_t count = entry->summary->parameter_count;
    if (count) {
        add_source(builder, 0, L"static const uint32_t goat_params_%zu[] = {", entry->id);
        for (size_t i = 0; i < count; i++)
            add_source(builder, 1, L"%s,", type_tag(c_generation_parameter_type(&context, i)));
        add_static_source(builder, 0, L"};");
    }
    add_source(
        builder,
        0,
        L"static uint32_t GOAT_NATIVE_CALL goat_adapter_%zu(uint32_t version, uint32_t count,",
        entry->id);
    add_static_source(builder,
                      1,
                      L"const goat_native_value_v1_t *args, goat_native_value_v1_t *result) {");
    add_static_source(builder,
                      1,
                      L"if (version != GOAT_NATIVE_ABI_VERSION) return GOAT_NATIVE_ABI_MISMATCH;");
    add_static_source(builder,
                      1,
                      L"if (!result || (count && !args)) return GOAT_NATIVE_BAD_REQUEST;");
    if (count) {
        add_source(builder, 1, L"if (count < %zu) return GOAT_NATIVE_TYPE_MISMATCH;", count);
        for (size_t i = 0; i < count; i++) {
            add_source(builder, 1, L"if (args[%zu].reserved) return GOAT_NATIVE_BAD_REQUEST;", i);
            add_source(builder,
                       1,
                       L"if (args[%zu].type != %s) return GOAT_NATIVE_TYPE_MISMATCH;",
                       i,
                       type_tag(c_generation_parameter_type(&context, i)));
        }
    }
    add_static_source(builder, 1, L"goat_native_guard_t guard = {.depth = 0, .fuel = 4096};");
    add_static_source(builder, 1, L"guard.origin = (uintptr_t)&guard;");
    add_static_source(builder, 1, L"goat_native_guard_t *previous = goat_guard;");
    add_static_source(builder, 1, L"if (setjmp(guard.recovery)) {");
    add_static_source(builder, 2, L"goat_guard = previous;");
    add_static_source(builder, 2, L"return GOAT_NATIVE_RESOURCE_LIMIT;");
    add_static_source(builder, 1, L"}");
    add_static_source(builder, 1, L"goat_guard = &guard;");
    add_static_source(builder, 1, L"goat_native_value_v1_t value = {0};");
    add_source(builder, 1, L"value.type = %s;", type_tag(c_generation_return_type(&context)));
    string_builder_t call;
    init_string_builder(&call, 64);
    append_substring(&call, entry->name.data, entry->name.length);
    append_char(&call, L'(');
    for (size_t i = 0; i < count; i++) {
        if (i)
            append_string(&call, L", ");
        string_value_t argument = format_string(
            L"args[%zu].value.%s",
            i,
            c_generation_parameter_type(&context, i) == C_VALUE_INT64 ? L"integer" : L"real");
        append_string_value(&call, argument);
        FREE_STRING(argument);
    }
    string_value_t expression = append_char(&call, L')');
    add_source(builder,
               1,
               L"value.value.%s = %s;",
               c_generation_return_type(&context) == C_VALUE_INT64 ? L"integer" : L"real",
               expression.data);
    FREE_STRING(expression);
    add_static_source(builder, 1, L"goat_guard = previous;");
    add_static_source(builder, 1, L"*result = value;");
    add_static_source(builder, 1, L"return GOAT_NATIVE_OK;");
    add_static_source(builder, 0, L"}");
}

void c_emit_native_module(source_builder_t *builder,
                          const c_module_t *module,
                          const c_generation_result_t *results,
                          size_t count) {
    if (count) {
        add_static_source(builder,
                          0,
                          L"static const goat_native_entry_v1_t goat_native_entries[] = {");
        for (const c_module_function_t *entry = module->head; entry; entry = entry->next) {
            if (results[entry->id].status != C_GENERATION_OK)
                continue;
            c_generation_context_t context = {.summary = entry->summary};
            string_value_t parameters = entry->summary->parameter_count
                                            ? format_string(L"goat_params_%zu", entry->id)
                                            : STATIC_STRING(L"0");
            add_source(builder,
                       1,
                       L"{%zu, %zu, %zu, %s, %s, goat_adapter_%zu, 0, GOAT_NATIVE_PURE},",
                       entry->id,
                       entry->function_id,
                       entry->summary->parameter_count,
                       type_tag(c_generation_return_type(&context)),
                       parameters.data,
                       entry->id);
            FREE_STRING(parameters);
        }
        add_static_source(builder, 0, L"};");
    }
    add_static_source(builder, 0, L"static const goat_native_module_v1_t goat_native_module = {");
    add_static_source(builder, 1, L"GOAT_NATIVE_ABI_VERSION, sizeof(goat_native_module_v1_t),");
    add_static_source(builder,
                      1,
                      L"sizeof(goat_native_value_v1_t), _Alignof(goat_native_value_v1_t),");
    add_static_source(builder, 1, L"sizeof(goat_native_entry_v1_t), sizeof(void *),");
    add_source(builder, 1, L"%zu, %s", count, count ? L"goat_native_entries" : L"0");
    add_static_source(builder, 0, L"};");
    add_static_source(builder,
                      0,
                      L"GOAT_NATIVE_API const goat_native_module_v1_t *GOAT_NATIVE_CALL "
                      L"goat_native_query_v1(uint32_t version) {");
    add_static_source(builder,
                      1,
                      L"return version == GOAT_NATIVE_ABI_VERSION ? &goat_native_module : 0;");
    add_static_source(builder, 0, L"}");
}
