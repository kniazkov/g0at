/** @file native_pipeline.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Connects numeric analysis proofs to owned runtime descriptors.
 */
#include "native_pipeline.h"

#include "c_module_output.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "model/native_library.h"
#include "native_compiler.h"

#include <stdio.h>
#include <string.h>

static void cleanup_workspace(void *workspace) {
    destroy_native_workspace(workspace);
}

static bool emitted(const c_module_output_t *output, const c_module_function_t *entry) {
    for (const c_module_failure_t *failure = output->failures; failure; failure = failure->next)
        if (failure->entry == entry)
            return false;
    return true;
}

static uint32_t tag(lattice_type_t type) {
    return is_integer_lattice_type(type) ? GOAT_NATIVE_I64 : GOAT_NATIVE_F64;
}

/** @brief Rejects an unrelated or incomplete compiler artifact before attaching any descriptor. */
static bool validate(const c_module_t *module,
                     const c_module_output_t *output,
                     native_library_t *library,
                     const bytecode_t *code) {
    if (get_native_library_entry_count(library) != output->generated_count)
        return false;
    for (uint32_t i = 0; i < get_native_library_entry_count(library); i++) {
        const goat_native_entry_v1_t *actual = get_native_library_entry(library, i);
        const c_module_function_t *expected = module->head;
        while (expected && expected->id != actual->specialization_id)
            expected = expected->next;
        if (!expected || !emitted(output, expected) || expected->function_id != actual->function_id
            || actual->flags != GOAT_NATIVE_PURE || actual->binding_name
            || expected->summary->parameter_count != actual->parameter_count
            || tag(expected->summary->return_type->type) != actual->return_type)
            return false;
        for (size_t j = 0; j < actual->parameter_count; j++)
            if (tag(expected->summary->parameter_types[j]->type) != actual->parameter_types[j])
                return false;
        instr_index_t index = get_function_bytecode_instruction(expected->summary->function);
        if (index != BAD_INSTR_INDEX
            && (index >= code->instructions_count || code->instructions[index].opcode != FUNC
                || code->instructions[index].arg0 != actual->parameter_count))
            return false;
    }
    return true;
}

native_prepare_result_t
prepare_native_execution(const node_t *root, bytecode_t *code, const char *compiler) {
    native_prepare_result_t result = {.status = NATIVE_PREPARE_BIND_ERROR};
    if (!root || !code || code->native_functions) {
        result.diagnostic =
            copy_native_library_diagnostic("Native preparation requires fresh bytecode");
        return result;
    }
    if (!compiler) {
#ifdef _WIN32
        compiler = "gcc";
#else
        compiler = "cc";
#endif
    }
    arena_t *arena = create_arena(32);
    c_module_t *module = create_c_module(arena, root);
    c_module_output_t output = generate_c_module(arena, module);
    result.omitted_specializations = output.omitted_count;
    native_workspace_t *workspace = NULL;
    native_library_result_t loaded = {0};
    if (!output.generated_count) {
        result.status = NATIVE_PREPARE_EMPTY;
        goto done;
    }
    workspace = create_native_workspace();
    if (!workspace) {
        result.status = NATIVE_PREPARE_IO_ERROR;
        result.diagnostic = copy_native_library_diagnostic("Cannot create native workspace");
        goto done;
    }
    native_compile_result_t compiled =
        compile_native_library(output.source.data, compiler, workspace->library);
    if (compiled.status != NATIVE_COMPILE_OK) {
        result.status = NATIVE_PREPARE_COMPILE_ERROR;
        result.diagnostic = copy_native_library_diagnostic(
            compiled.diagnostics && *compiled.diagnostics ? compiled.diagnostics
                                                          : "Native compilation failed");
        destroy_native_compile_result(&compiled);
        goto done;
    }
    destroy_native_compile_result(&compiled);
    loaded = load_native_library(workspace->library);
    if (loaded.status != NATIVE_LIBRARY_OK) {
        result.status = NATIVE_PREPARE_LOAD_ERROR;
        result.diagnostic = copy_native_library_diagnostic(loaded.diagnostic);
        goto done;
    }
    set_native_library_cleanup(loaded.library, workspace, cleanup_workspace);
    workspace = NULL;
    if (!validate(module, &output, loaded.library, code)) {
        result.status = NATIVE_PREPARE_BIND_ERROR;
        result.diagnostic =
            copy_native_library_diagnostic("Generated native metadata does not match bytecode");
        goto done;
    }
    for (const c_module_function_t *entry = module->head; entry; entry = entry->next) {
        instr_index_t index = get_function_bytecode_instruction(entry->summary->function);
        if (!emitted(&output, entry) || index == BAD_INSTR_INDEX
            || get_bytecode_native_function(code, index))
            continue;
        native_function_descriptor_t *function =
            create_native_function_descriptor(loaded.library, entry->function_id);
        /* Validation above makes binding infallible for this fresh code. */
        bool bound = bind_bytecode_native_function(code, index, function);
        release_native_function_descriptor(function);
        if (!bound) {
            result.status = NATIVE_PREPARE_BIND_ERROR;
            for (size_t i = 0; i < code->instructions_count; i++)
                if (code->instructions[i].opcode == FUNC)
                    bind_bytecode_native_function(code, i, NULL);
            result.bound_functions = 0;
            goto done;
        }
        result.bound_functions++;
    }
    result.status = result.bound_functions ? NATIVE_PREPARE_READY : NATIVE_PREPARE_EMPTY;
done:
    destroy_native_library_result(&loaded);
    destroy_native_workspace(workspace);
    FREE_STRING(output.source);
    destroy_arena(arena);
    return result;
}

void destroy_native_prepare_result(native_prepare_result_t *result) {
    FREE(result->diagnostic);
    result->diagnostic = NULL;
}
