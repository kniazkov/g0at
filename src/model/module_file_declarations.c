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
    bool *function_body;
    bool valid, syntactic;
} declaration_walk_t;

static void enqueue(declaration_walk_t *walk, size_t address, size_t scope) {
    if (address < walk->first || address - walk->first >= walk->count) {
        walk->valid = false;
        return;
    }
    size_t offset = address - walk->first;
    if (walk->syntactic && walk->function_body[offset])
        return;
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
            case FINALLY_END:
                continue;
            case RET:
            case THROW:
                if (!walk->syntactic || address + 1 == walk->first + walk->count)
                    continue;
                break;
            case JUMP:
                enqueue(walk, instr.arg1, scope);
                if (!walk->syntactic || address + 1 == walk->first + walk->count)
                    continue;
                break;
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
            case LOOP_JUMP: {
                size_t lexical_scope = scope;
                while (scope
                       && walk->code->instructions[walk->first + scope - 1].opcode != LOOP_ENTER)
                    scope = walk->parent[scope - 1];
                if (!scope)
                    walk->valid = false;
                else
                    enqueue(walk, instr.arg1, scope);
                if (walk->syntactic && address + 1 < walk->first + walk->count)
                    enqueue(walk, address + 1, lexical_scope);
                continue;
            }
            default:
                break;
        }
        enqueue(walk, address + 1, scope);
    }
}

static bool visit_declarations(const bytecode_t *code,
                               uint32_t file,
                               bool syntactic,
                               module_declaration_visitor_t visitor,
                               void *context,
                               bool legacy) {
    if (!code || file >= code->file_count || !visitor)
        return false;
    bytecode_file_t record = code->files[file];
    if (record.count > SIZE_MAX / sizeof(size_t))
        return false;
    declaration_walk_t walk = {.code = code,
                               .first = record.first,
                               .count = record.count,
                               .valid = true,
                               .syntactic = syntactic};
    walk.scope = ALLOC(record.count * sizeof(size_t));
    walk.parent = CALLOC(record.count * sizeof(size_t));
    walk.queue = ALLOC(record.count * sizeof(size_t));
    for (size_t i = 0; i < record.count; ++i)
        walk.scope[i] = SIZE_MAX;
    if (syntactic) {
        walk.function_body = CALLOC(record.count * sizeof(bool));
        for (size_t i = 0; i < code->location_count; ++i) {
            bytecode_location_t location = code->locations[i];
            if (location.file == file && location.function)
                for (size_t j = 0; j < location.count; ++j)
                    walk.function_body[location.first + j - record.first] = true;
        }
    }
    scan(&walk, record.entry);
    for (size_t i = 0; walk.valid && i < record.count; ++i) {
        size_t address = record.first + i;
        instruction_t instr = code->instructions[address];
        bool plain = legacy && (instr.opcode == VAR || instr.opcode == CONST);
        if (walk.scope[i] != 0 || (!plain && instr.opcode != DVAR && instr.opcode != DCONST))
            continue;
        declaration_descriptor_t declaration;
        if (plain) {
            declaration = (declaration_descriptor_t){.name = instr.arg1, .modifier = ACCESS_FILE};
            walk.valid = instr.arg1 < code->data_descriptor_count;
        } else {
            walk.valid = get_bytecode_declaration(code, instr.arg1, &declaration);
        }
        walk.valid = walk.valid && visitor(code, address, &declaration, context);
    }
    FREE(walk.function_body);
    FREE(walk.queue);
    FREE(walk.parent);
    FREE(walk.scope);
    return walk.valid;
}

typedef struct {
    object_t *scope;
    vector_t *names, *indices;
    bool remaining;
} pending_declarations_t;

static bool reserve_name(const bytecode_t *code,
                         size_t address,
                         const declaration_descriptor_t *declaration,
                         void *context) {
    pending_declarations_t *pending = context;
    data_descriptor_t text = code->data_descriptors[declaration->name];
    object_t *key =
        create_string_object(pending->scope->process,
                             (string_value_t){(const gchar_t *)(code->data + text.offset),
                                              text.size / sizeof(gchar_t) - 1,
                                              false});
    field_cell_t *existing = get_own_field_cell(pending->scope, key);
    if (existing && pending->remaining) {
        DECREF(key);
        return true;
    }
    bool valid = !existing;
    for (size_t n = 0; n < pending->names->size; ++n)
        if (!compare_objects_using_vtbl(pending->names->data[n], key))
            valid = false;
    append_to_vector(pending->names, key);
    append_to_vector(pending->indices, (void *)(uintptr_t)(address + 1));
    return valid;
}

static bool prepare_scope(bytecode_t *code, uint32_t file_id, object_t *scope, bool remaining) {
    if (!code || !scope || file_id >= code->file_count)
        return false;
    /* Validate all names before publishing any cells. */
    vector_t *names = create_vector();
    vector_t *indices = create_vector();
    pending_declarations_t pending = {scope, names, indices, remaining};
    bool valid = visit_declarations(code, file_id, false, reserve_name, &pending, true);
    if (valid) {
        if (!code->declaration_unit)
            code->declaration_unit = create_module_identity();
        for (size_t n = 0; n < names->size; ++n) {
            size_t i = (size_t)(uintptr_t)indices->data[n] - 1;
            instruction_t instr = code->instructions[i];
            declaration_descriptor_t declaration;
            bool plain = instr.opcode == VAR || instr.opcode == CONST;
            if (plain)
                declaration =
                    (declaration_descriptor_t){.name = instr.arg1, .modifier = ACCESS_FILE};
            else
                get_bytecode_declaration(code, instr.arg1, &declaration);
            field_cell_t *cell =
                create_object_field_in_file(scope,
                                            names->data[n],
                                            get_null_object(),
                                            instr.opcode == DCONST || instr.opcode == CONST,
                                            (access_modifier_t)declaration.modifier,
                                            get_object_module(scope),
                                            file_id,
                                            get_object_access_domain(scope),
                                            true);
            if (!cell) {
                valid = false;
                break;
            }
            cell->pending_initialization = true;
            cell->declaration_id = plain ? 0 : instr.arg1 + 1;
            cell->binding_unit = retain_module_identity(code->declaration_unit);
        }
    }
    for (size_t n = 0; n < names->size; ++n)
        DECREF((object_t *)names->data[n]);
    destroy_vector(indices);
    destroy_vector(names);
    return valid;
}

bool prepare_module_file_declarations(module_file_t *file) {
    return file
           && prepare_file_scope_declarations(module_file_bytecode(file),
                                              module_file_id(file),
                                              module_file_scope(file));
}

bool visit_module_file_declarations(const bytecode_t *code,
                                    uint32_t file,
                                    bool syntactic,
                                    module_declaration_visitor_t visitor,
                                    void *context) {
    return visit_declarations(code, file, syntactic, visitor, context, false);
}

bool prepare_file_scope_declarations(bytecode_t *code, uint32_t file_id, object_t *scope) {
    return prepare_scope(code, file_id, scope, false);
}

bool prepare_remaining_file_scope_declarations(bytecode_t *code,
                                               uint32_t file_id,
                                               object_t *scope) {
    return prepare_scope(code, file_id, scope, true);
}
