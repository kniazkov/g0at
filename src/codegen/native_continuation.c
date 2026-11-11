/** @file native_continuation.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Native PC dispatch with per-instruction transitions into shared semantics.
 */
#include "native_continuation.h"

#include "graph/expression.h"
#include "lib/memory/allocate.h"

static const node_t *function_node(const node_t *node, uint32_t instruction) {
    if (!node)
        return NULL;
    if (node->vtbl->type == NODE_FUNCTION_OBJECT
        && get_function_bytecode_instruction(node) == instruction)
        return node;
    for (size_t i = 0; i < get_node_child_count(node); ++i) {
        const node_t *found = function_node(get_node_child(node, i), instruction);
        if (found)
            return found;
    }
    return NULL;
}

static bool scalar_only(const node_t *root,
                        uint32_t instruction,
                        uint32_t id,
                        const c_module_t *module,
                        const c_module_output_t *scalar) {
    (void)scalar;
    bool emitted = false;
    const node_t *known_function = NULL;
    for (const c_module_function_t *entry = module ? module->head : NULL; entry;
         entry = entry->next) {
        if (entry->function_id != id)
            continue;
        known_function = entry->summary->function;
        /* A typed scalar lowering rejected for backend limits remains a VM fallback.
         * This extension is for dynamic semantics, not bypassing scalar resource guards. */
        emitted = true;
    }
    if (!emitted)
        return false;
    const node_t *node = function_node(root, instruction);
    if (!node)
        node = known_function;
    const function_summary_set_t *set = node ? get_function_summaries(node) : NULL;
    for (const function_summary_t *s = set ? set->head : NULL; s; s = s->next) {
        if (s->c_support != FUNCTION_C_SUPPORTED || s->c_blockers)
            return false;
    }
    return true;
}

static bool dynamic_operation(uint8_t opcode) {
    return (opcode >= UPLUS && opcode <= DIFF) || opcode == CALL || opcode == CALLM
           || opcode == GETINDEX || opcode == SETINDEX || opcode == CHECKINDEX;
}

native_continuation_output_t generate_native_continuations(const node_t *root,
                                                           const bytecode_t *code,
                                                           const c_module_t *module,
                                                           const c_module_output_t *scalar) {
    native_continuation_output_t output = {0};
    if (!code || !code->function_count || code->instructions_count > UINT32_MAX)
        return output;
    output.entries = CALLOC(code->function_count * sizeof(*output.entries));
    for (size_t i = 0; i < code->function_count; ++i) {
        const bytecode_function_t *function = &code->functions[i];
        uint32_t end = (uint32_t)code->instructions_count;
        if (code->files && function->file < code->file_count) {
            const bytecode_file_t *file = &code->files[function->file];
            uint64_t file_end = (uint64_t)file->first + file->count;
            if (function->body < file->first || file_end > code->instructions_count
                || function->body >= file_end)
                continue;
            end = (uint32_t)file_end;
        }
        for (size_t j = 0; j < code->function_count; ++j)
            if (code->functions[j].body > function->body && code->functions[j].body < end)
                end = code->functions[j].body;
        if (function->body >= end
            || scalar_only(root, function->instruction, function->id, module, scalar))
            continue;
        bool needed = false;
        for (uint32_t pc = function->body; pc < end; ++pc)
            needed |= dynamic_operation(code->instructions[pc].opcode);
        if (!needed)
            continue;
        output.entries[output.count++] =
            (goat_native_continuation_entry_t){.function_id = function->id,
                                               .function_instruction = function->instruction,
                                               .first_instruction = function->body,
                                               .instruction_count = end - function->body};
    }
    if (!output.count) {
        FREE(output.entries);
        output.entries = NULL;
        return output;
    }
    source_builder_t *builder = create_source_builder();
    add_static_source(builder, 0, U"#include \"native_continuation.h\"");
    for (size_t i = 0; i < output.count; ++i) {
        const goat_native_continuation_entry_t *entry = &output.entries[i];
        add_source(builder,
                   0,
                   U"static uint32_t GOAT_NATIVE_CALL g_continuation_%zu("
                   U"const goat_native_continuation_context_t *context, uint32_t pc) {",
                   i);
        add_static_source(builder,
                          1,
                          U"if (!context || !context->step) return GOAT_NATIVE_FAILED;");
        add_static_source(builder, 1, U"uint32_t next = 0, status;");
        add_static_source(builder, 1, U"for (;;) {");
        add_static_source(builder, 2, U"switch (pc) {");
        for (uint32_t pc = entry->first_instruction;
             pc < entry->first_instruction + entry->instruction_count;
             ++pc) {
            add_source(builder, 2, U"case %u:", pc);
            add_source(builder, 3, U"status = context->step(context->state, %u, &next);", pc);
            add_static_source(builder, 3, U"if (status != GOAT_NATIVE_CONTINUE) return status;");
            add_static_source(builder, 3, U"pc = next; break;");
        }
        add_static_source(builder, 2, U"default: return GOAT_NATIVE_FAILED;");
        add_static_source(builder, 2, U"}");
        add_static_source(builder, 1, U"}");
        add_static_source(builder, 0, U"}");
    }
    add_static_source(
        builder,
        0,
        U"static const goat_native_continuation_entry_t g_continuation_entries[] = {");
    for (size_t i = 0; i < output.count; ++i) {
        const goat_native_continuation_entry_t *entry = &output.entries[i];
        add_source(builder,
                   1,
                   U"{%u, %u, %u, %u, g_continuation_%zu},",
                   (uint32_t)entry->function_id,
                   entry->function_instruction,
                   entry->first_instruction,
                   entry->instruction_count,
                   i);
    }
    add_static_source(builder, 0, U"};");
    add_static_source(builder,
                      0,
                      U"static const goat_native_continuation_module_t g_continuation_module = {");
    add_source(builder,
               1,
               U"GOAT_NATIVE_CONTINUATION_VERSION, sizeof(goat_native_continuation_module_t), "
               U"sizeof(goat_native_continuation_entry_t), %zu, g_continuation_entries",
               output.count);
    add_static_source(builder, 0, U"};");
    add_static_source(builder,
                      0,
                      U"GOAT_NATIVE_API const goat_native_continuation_module_t *GOAT_NATIVE_CALL "
                      U"goat_native_continuation_query(uint32_t version) {");
    add_static_source(builder,
                      1,
                      U"return version == GOAT_NATIVE_CONTINUATION_VERSION "
                      U"? &g_continuation_module : 0;");
    add_static_source(builder, 0, U"}");
    output.source = build_source(builder);
    destroy_source_builder(builder);
    return output;
}

void destroy_native_continuation_output(native_continuation_output_t *output) {
    FREE_STRING(output->source);
    FREE(output->entries);
    *output = (native_continuation_output_t){0};
}
