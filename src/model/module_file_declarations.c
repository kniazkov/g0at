/** @file module_file_declarations.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Non-executing scope analysis for pending file declaration cells.
 */
#include "module_file_declarations.h"

#include "lib/allocate.h"

#include <stdint.h>

/** Each instruction is visited once; a scope is identified by its opening instruction. */
typedef struct {
    const bytecode_t *code;
    size_t first, count, used;
    size_t *scope, *parent, *queue;
    bool valid;
} declaration_walk_t;

static void enqueue(declaration_walk_t *walk, size_t address, size_t scope) {
    if (address < walk->first || address - walk->first >= walk->count) {
        walk->valid = false;
        return;
    }
    size_t offset = address - walk->first;
    if (walk->scope[offset] != SIZE_MAX) {
        if (walk->scope[offset] != scope)
            walk->valid = false;
        return;
    }
    walk->scope[offset] = scope;
    walk->queue[walk->used++] = address;
}

static void scan(declaration_walk_t *walk, size_t entry) {
    enqueue(walk, entry, 0);
    for (size_t next = 0; walk->valid && next < walk->used; ++next) {
        size_t address = walk->queue[next];
        size_t offset = address - walk->first;
        size_t scope = walk->scope[offset];
        instruction_t instr = walk->code->instructions[address];
        switch (instr.opcode) {
            case END:
            case RET:
            case THROW:
            case FINALLY_END:
                continue;
            case JUMP:
                enqueue(walk, instr.arg1, scope);
                continue;
            case JIF:
            case LAND:
            case LOR:
            case ITER_NEXT:
                enqueue(walk, instr.arg1, scope);
                break;
            case TRY:
                /* Exception dispatch removes the protected frame before the handler. */
                enqueue(walk, instr.arg1, scope);
                walk->parent[offset] = scope;
                scope = offset + 1;
                break;
            case FINALLY_ENTER:
                walk->parent[offset] = scope;
                scope = offset + 1;
                /* Cleanup executes inside the same frame, later removed by END. */
                enqueue(walk, instr.arg1, scope);
                break;
            case ENTER:
            case INHERIT:
            case LOOP_ENTER:
                walk->parent[offset] = scope;
                scope = offset + 1;
                break;
            case RESTORE:
            case LEAVE:
                if (!scope) {
                    walk->valid = false;
                    continue;
                }
                scope = walk->parent[scope - 1];
                break;
            case FINALLY_LEAVE:
                if (!scope
                    || walk->code->instructions[walk->first + scope - 1].opcode != FINALLY_ENTER) {
                    walk->valid = false;
                    continue;
                }
                /* Visit normal continuation as well as the cleanup entry queued above. */
                enqueue(walk, instr.arg1, walk->parent[scope - 1]);
                continue;
            case LOOP_JUMP:
                while (scope
                       && walk->code->instructions[walk->first + scope - 1].opcode != LOOP_ENTER)
                    scope = walk->parent[scope - 1];
                if (!scope)
                    walk->valid = false;
                else
                    enqueue(walk, instr.arg1, scope);
                continue;
            default:
                break;
        }
        enqueue(walk, address + 1, scope);
    }
}

bool prepare_module_file_declarations(module_file_t *file) {
    if (!file)
        return false;
    bytecode_t *code = module_file_bytecode(file);
    bytecode_file_t record = code->files[module_file_id(file)];
    size_t count = record.count;
    if (count > SIZE_MAX / sizeof(size_t))
        return false;
    declaration_walk_t walk = {.code = code,
                               .first = record.first,
                               .count = record.count,
                               .valid = true};
    walk.scope = ALLOC(record.count * sizeof(size_t));
    walk.parent = CALLOC(record.count * sizeof(size_t));
    walk.queue = ALLOC(record.count * sizeof(size_t));
    for (size_t i = 0; i < record.count; ++i)
        walk.scope[i] = SIZE_MAX;
    scan(&walk, record.entry);
    object_t *scope = module_file_scope(file);
    /* Validate all names before publishing any cells. */
    vector_t *names = create_vector();
    vector_t *indices = create_vector();
    for (size_t i = 0; walk.valid && i < record.count; ++i) {
        instruction_t instr = code->instructions[record.first + i];
        if (walk.scope[i] != 0 || (instr.opcode != DVAR && instr.opcode != DCONST))
            continue;
        declaration_descriptor_t declaration;
        if (!get_bytecode_declaration(code, instr.arg1, &declaration)) {
            walk.valid = false;
            break;
        }
        data_descriptor_t text = code->data_descriptors[declaration.name];
        object_t *key =
            create_string_object(scope->process,
                                 (string_value_t){(const gchar_t *)(code->data + text.offset),
                                                  text.size / sizeof(gchar_t) - 1,
                                                  false});
        if (get_own_field_cell(scope, key))
            walk.valid = false;
        for (size_t n = 0; n < names->size; ++n)
            if (!compare_objects_using_vtbl(names->data[n], key))
                walk.valid = false;
        append_to_vector(names, key);
        append_to_vector(indices, (void *)(uintptr_t)(i + 1));
    }
    if (walk.valid) {
        if (!code->declaration_unit)
            code->declaration_unit = create_module_identity();
        for (size_t n = 0; n < names->size; ++n) {
            size_t i = (size_t)(uintptr_t)indices->data[n] - 1;
            instruction_t instr = code->instructions[record.first + i];
            declaration_descriptor_t declaration;
            get_bytecode_declaration(code, instr.arg1, &declaration);
            field_cell_t *cell =
                create_object_field_in_file(scope,
                                            names->data[n],
                                            get_null_object(),
                                            instr.opcode == DCONST,
                                            (access_modifier_t)declaration.modifier,
                                            get_object_module(scope),
                                            module_file_id(file),
                                            get_object_access_domain(scope),
                                            true);
            if (!cell) {
                walk.valid = false;
                break;
            }
            cell->pending_initialization = true;
            cell->declaration_id = instr.arg1 + 1;
            cell->binding_unit = retain_module_identity(code->declaration_unit);
        }
    }
    for (size_t n = 0; n < names->size; ++n)
        DECREF((object_t *)names->data[n]);
    destroy_vector(indices);
    destroy_vector(names);
    FREE(walk.queue);
    FREE(walk.parent);
    FREE(walk.scope);
    return walk.valid;
}
