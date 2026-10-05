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
    static const wchar_t *const lines[] = {
        L"/* ABI declarations begin. */",
        L"#ifndef GOAT_NATIVE_ABI_V1_H",
        L"#define GOAT_NATIVE_ABI_V1_H",
        L"#include <stdint.h>",
        L"",
        L"#ifdef _WIN32",
        L"#define GOAT_NATIVE_CALL __cdecl",
        L"#ifdef GOAT_NATIVE_BUILD",
        L"#define GOAT_NATIVE_API __declspec(dllexport)",
        L"#else",
        L"#define GOAT_NATIVE_API",
        L"#endif",
        L"#else",
        L"#define GOAT_NATIVE_CALL",
        L"#define GOAT_NATIVE_API",
        L"#endif",
        L"",
        L"enum {",
        L"    GOAT_NATIVE_ABI_VERSION = 1,",
        L"    GOAT_NATIVE_INVALID = 0,",
        L"    GOAT_NATIVE_I64 = 1,",
        L"    GOAT_NATIVE_F64 = 2",
        L"};",
        L"",
        L"enum {",
        L"    GOAT_NATIVE_OK = 0,",
        L"    GOAT_NATIVE_TYPE_MISMATCH = 1,",
        L"    GOAT_NATIVE_BAD_REQUEST = 2,",
        L"    GOAT_NATIVE_ABI_MISMATCH = 3,",
        L"    GOAT_NATIVE_RESOURCE_LIMIT = 4,",
        L"    GOAT_NATIVE_EXTERNAL_ERROR = 5",
        L"};",
        L"",
        L"enum { GOAT_NATIVE_PURE = 1 };",
        L"",
        L"#ifdef __cplusplus",
        L"extern \"C\" {",
        L"#endif",
        L"",
        L"",
        L"typedef struct goat_native_value_v1_t {",
        L"    uint32_t type;",
        L"    uint32_t reserved;",
        L"",
        L"    union {",
        L"        int64_t integer;",
        L"        double real;",
        L"    } value;",
        L"} goat_native_value_v1_t;",
        L"",
        L"typedef uint32_t (GOAT_NATIVE_CALL *goat_native_adapter_v1_t)(uint32_t version,",
        L"                                             uint32_t argument_count,",
        L"                                             const goat_native_value_v1_t *arguments,",
        L"                                             goat_native_value_v1_t *result);",
        L"",
        L"",
        L"typedef struct goat_native_entry_v1_t {",
        L"    uint64_t specialization_id;",
        L"    uint64_t function_id;",
        L"    uint32_t parameter_count;",
        L"    uint32_t return_type;",
        L"    const uint32_t *parameter_types;",
        L"    goat_native_adapter_v1_t invoke;",
        L"    const char *binding_name;",
        L"    uint32_t flags;",
        L"} goat_native_entry_v1_t;",
        L"",
        L"",
        L"typedef struct goat_native_module_v1_t {",
        L"    uint32_t abi_version;",
        L"    uint32_t struct_size;",
        L"    uint32_t value_size;",
        L"    uint32_t value_alignment;",
        L"    uint32_t entry_size;",
        L"    uint32_t pointer_size;",
        L"    uint32_t entry_count;",
        L"    const goat_native_entry_v1_t *entries;",
        L"} goat_native_module_v1_t;",
        L"",
        L"typedef const goat_native_module_v1_t *(GOAT_NATIVE_CALL "
        L"*goat_native_query_v1_t)(uint32_t version);",
        L"",
        L"GOAT_NATIVE_API const goat_native_module_v1_t *GOAT_NATIVE_CALL "
        L"goat_native_query_v1(uint32_t version);",
        L"#ifdef __cplusplus",
        L"}",
        L"#endif",
        L"#endif",
        L"/* ABI declarations end. */",
    };
    add_source_lines(builder, 0, lines, sizeof(lines) / sizeof(*lines));
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
        L"    uintptr_t origin;",
        L"} g_native_guard_t;",
        L"",
        L"static _Thread_local g_native_guard_t *g_guard;",
        L"",
        L"static inline void g_guard_enter(void) {",
        L"    if (!g_guard) return;",
        L"    char position;",
        L"    uintptr_t here = (uintptr_t)&position;",
        L"    uintptr_t distance = here > g_guard->origin ? here - g_guard->origin : "
        L"g_guard->origin - here;",
        L"    if (g_guard->depth >= 32 || distance >= 65536)",
        L"        longjmp(g_guard->recovery, 1);",
        L"    g_guard->depth++;",
        L"}",
        L"",
        L"static inline void g_guard_leave(void) {",
        L"    if (g_guard) g_guard->depth--;",
        L"}"};
    add_source_lines(builder, 0, lines, sizeof(lines) / sizeof(*lines));
}

static const wchar_t *type_tag(c_value_type_t type) {
    return type == C_VALUE_INT64 ? L"GOAT_NATIVE_I64" : L"GOAT_NATIVE_F64";
}

void c_emit_adapter(source_builder_t *builder, const c_module_function_t *entry) {
    c_generation_context_t context = {.summary = entry->summary};
    size_t count = entry->summary->parameter_count;
    add_static_source(builder, 0, L"");
    if (count) {
        add_source(builder, 0, L"static const uint32_t g_params_%zu[] = {", entry->id);
        for (size_t i = 0; i < count; i++)
            add_source(builder, 1, L"%s,", type_tag(c_generation_parameter_type(&context, i)));
        add_static_source(builder, 0, L"};");
    }
    add_static_source(builder, 0, L"");
    add_source(builder, 0, L"/* Native ABI adapter for %s. */", entry->name.data);
    add_source(builder,
               0,
               L"static uint32_t GOAT_NATIVE_CALL g_adapter_%zu(uint32_t version, uint32_t count,",
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
    add_static_source(builder, 1, L"g_native_guard_t guard = {.depth = 0};");
    add_static_source(builder, 1, L"guard.origin = (uintptr_t)&guard;");
    add_static_source(builder, 1, L"g_native_guard_t *previous = g_guard;");
    add_static_source(builder, 1, L"if (setjmp(guard.recovery)) {");
    add_static_source(builder, 2, L"g_guard = previous;");
    add_static_source(builder, 2, L"return GOAT_NATIVE_RESOURCE_LIMIT;");
    add_static_source(builder, 1, L"}");
    add_static_source(builder, 1, L"g_guard = &guard;");
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
    add_static_source(builder, 1, L"g_guard = previous;");
    add_static_source(builder, 1, L"*result = value;");
    add_static_source(builder, 1, L"return GOAT_NATIVE_OK;");
    add_static_source(builder, 0, L"}");
}

void c_emit_native_module(source_builder_t *builder,
                          const c_module_t *module,
                          const c_generation_result_t *results,
                          size_t count) {
    add_static_source(builder, 0, L"");
    if (count) {
        add_static_source(builder,
                          0,
                          L"static const goat_native_entry_v1_t g_native_entries[] = {");
        for (const c_module_function_t *entry = module->head; entry; entry = entry->next) {
            if (results[entry->id].status != C_GENERATION_OK)
                continue;
            c_generation_context_t context = {.summary = entry->summary};
            string_value_t parameters = entry->summary->parameter_count
                                            ? format_string(L"g_params_%zu", entry->id)
                                            : STATIC_STRING(L"0");
            add_source(builder,
                       1,
                       L"{%zu, %zu, %zu, %s, %s, g_adapter_%zu, 0, GOAT_NATIVE_PURE},",
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
    add_static_source(builder, 0, L"");
    add_static_source(builder, 0, L"static const goat_native_module_v1_t g_native_module = {");
    add_static_source(builder, 1, L"GOAT_NATIVE_ABI_VERSION, sizeof(goat_native_module_v1_t),");
    add_static_source(builder,
                      1,
                      L"sizeof(goat_native_value_v1_t), _Alignof(goat_native_value_v1_t),");
    add_static_source(builder, 1, L"sizeof(goat_native_entry_v1_t), sizeof(void *),");
    add_source(builder, 1, L"%zu, %s", count, count ? L"g_native_entries" : L"0");
    add_static_source(builder, 0, L"};");
    add_static_source(builder, 0, L"");
    add_static_source(builder,
                      0,
                      L"GOAT_NATIVE_API const goat_native_module_v1_t *GOAT_NATIVE_CALL "
                      L"goat_native_query_v1(uint32_t version) {");
    add_static_source(builder,
                      1,
                      L"return version == GOAT_NATIVE_ABI_VERSION ? &g_native_module : 0;");
    add_static_source(builder, 0, L"}");
}
