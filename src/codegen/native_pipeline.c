/** @file native_pipeline.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Connects numeric analysis proofs to owned runtime descriptors.
 */
#include "native_pipeline.h"

#include "analysis/access.h"
#include "c_module_output.h"
#include "graph/expression.h"
#include "lib/allocate.h"
#include "lib/binary_file.h"
#include "model/native_library.h"
#include "native_compiler.h"
#include "native_toolchain.h"
#include "vm/binary.h"

#include <stdio.h>
#include <string.h>

static bool has_access_blocker(const node_t *node) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT && function_needs_access_checks(node))
        return true;
    for (size_t i = 0; i < get_node_child_count(node); i++)
        if (has_access_blocker(get_node_child(node, i)))
            return true;
    return false;
}

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
    return is_integer_lattice_type(type) ? GOAT_NATIVE_I64
           : is_real_lattice_type(type)  ? GOAT_NATIVE_F64
           : type == LATTICE_BOOLEAN     ? GOAT_NATIVE_BOOL
           : type == LATTICE_CHAR        ? GOAT_NATIVE_CHAR
                                         : GOAT_NATIVE_STRING;
}

/** @brief Rejects an unrelated or incomplete compiler artifact before attaching any descriptor. */
static bool validate(const c_module_t *module,
                     const c_module_output_t *output,
                     native_library_t *library,
                     const bytecode_t *code) {
    if (get_native_library_entry_count(library) != output->generated_count)
        return false;
    for (uint32_t i = 0; i < get_native_library_entry_count(library); i++) {
        const goat_native_entry_v2_t *actual = get_native_library_entry(library, i);
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

static native_prepare_result_t prepare(const node_t *root,
                                       bytecode_t *code,
                                       const char *compiler,
                                       const char *destination,
                                       native_build_context_t *context,
                                       const native_compile_options_t *options,
                                       uint32_t probe_timeout) {
    native_prepare_result_t result = {.status = NATIVE_PREPARE_BIND_ERROR};
    if (!root || !code || code->native_functions) {
        result.diagnostic =
            copy_native_library_diagnostic("Native preparation requires fresh bytecode");
        return result;
    }
    if (!compiler && !probe_timeout) {
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
    native_toolchain_context_t *toolchains = NULL;
    native_toolchain_search_t search = {0};
    native_compile_options_t selected_options = {0};
    if (!output.generated_count) {
        result.status = NATIVE_PREPARE_EMPTY;
        if (has_access_blocker(root))
            result.diagnostic = copy_native_library_diagnostic(
                "Access checks require VM execution; no safe native function could be bound");
        goto done;
    }
    if (!context || ensure_cleanup_service(native_build_cleanup(context)) != CLEANUP_OK) {
        result.status = NATIVE_PREPARE_CLEANUP_ERROR;
        result.diagnostic = copy_native_library_diagnostic(
            "Temporary native cleanup service did not become ready; compiler was not started");
        goto done;
    }
    if (probe_timeout) {
        toolchains = create_native_toolchain_context(native_build_cleanup(context));
        search = find_native_toolchains(toolchains, "host", compiler, probe_timeout, false);
        if (!search.selected) {
            result.status = NATIVE_PREPARE_UNAVAILABLE;
            result.diagnostic = copy_native_library_diagnostic(
                search.diagnostic ? search.diagnostic
                                  : "No suitable native compiler was discovered");
            goto done;
        }
        compiler = search.selected->compiler;
        selected_options.arguments = search.selected->flags;
        selected_options.runtime_sources = true;
        options = &selected_options;
    }
    workspace = create_native_workspace_in_context(context);
    if (!workspace) {
        result.status = NATIVE_PREPARE_IO_ERROR;
        result.diagnostic = copy_native_library_diagnostic("Cannot create native workspace");
        goto done;
    }
    native_compile_result_t compiled = compile_native_library_in_context(context,
                                                                         output.source.data,
                                                                         compiler,
                                                                         workspace->library,
                                                                         options);
    if (compiled.status != NATIVE_COMPILE_OK) {
        result.status = compiled.status == NATIVE_COMPILE_CLEANUP_ERROR
                            ? NATIVE_PREPARE_CLEANUP_ERROR
                            : NATIVE_PREPARE_COMPILE_ERROR;
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
    if (!validate(module, &output, loaded.library, code)) {
        result.status = NATIVE_PREPARE_BIND_ERROR;
        result.diagnostic =
            copy_native_library_diagnostic("Generated native metadata does not match bytecode");
        goto done;
    }
    if (destination) {
        size_t size;
        void *bytes = read_binary_file(workspace->library, GOAT_BINARY_LIMIT, &size);
        bool written = bytes && write_binary_file(destination, bytes, size);
        FREE(bytes);
        if (!written) {
            result.status = NATIVE_PREPARE_IO_ERROR;
            result.diagnostic = copy_native_library_diagnostic("Cannot save native library");
            goto done;
        }
    }
    set_native_library_cleanup(loaded.library, workspace, cleanup_workspace);
    workspace = NULL;
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
    destroy_native_toolchain_search(&search);
    destroy_native_toolchain_context(toolchains);
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

native_prepare_result_t
prepare_native_execution(const node_t *root, bytecode_t *code, const char *compiler) {
    native_build_context_t *context = create_native_build_context(NULL);
    native_prepare_result_t result = prepare(root, code, compiler, NULL, context, NULL, 0);
    release_native_build_context(context);
    return result;
}

native_prepare_result_t prepare_native_artifact(const node_t *root,
                                                bytecode_t *code,
                                                const char *compiler,
                                                const char *destination) {
    native_build_context_t *context = create_native_build_context(NULL);
    native_prepare_result_t result = prepare(root, code, compiler, destination, context, NULL, 0);
    release_native_build_context(context);
    return result;
}

native_prepare_result_t
prepare_native_execution_in_context(const node_t *root,
                                    bytecode_t *code,
                                    const char *compiler,
                                    native_build_context_t *context,
                                    const native_compile_options_t *options) {
    return prepare(root, code, compiler, NULL, context, options, 0);
}

native_prepare_result_t
prepare_native_artifact_in_context(const node_t *root,
                                   bytecode_t *code,
                                   const char *compiler,
                                   const char *destination,
                                   native_build_context_t *context,
                                   const native_compile_options_t *options) {
    return prepare(root, code, compiler, destination, context, options, 0);
}

native_prepare_result_t prepare_native_discovered(const node_t *root,
                                                  bytecode_t *code,
                                                  const char *compiler,
                                                  const char *destination,
                                                  native_build_context_t *context,
                                                  uint32_t probe_timeout) {
    return prepare(root, code, compiler, destination, context, NULL, probe_timeout);
}
