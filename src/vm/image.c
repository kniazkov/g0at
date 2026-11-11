/** @file image.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared checked decoder for standalone and module v1 bytecode images.
 */
#include "bytecode.h"
#include "lib/memory/allocate.h"
#include "lib/wire/wire.h"

#include <float.h>
#include <string.h>

_Static_assert(sizeof(double) == 8 && FLT_RADIX == 2 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024
                   && sizeof(int64_t) == 8 && INT64_MIN == -INT64_MAX - 1,
               "binary64/int64 required");

static bool string_index(const bytecode_t *c, uint32_t index) {
    return index < c->data_descriptor_count && c->data_descriptors[index].kind == DATA_STRING;
}

/** @brief Called only after all string payload bounds/scalars have been checked. */
static bool launch_constant(const bytecode_t *c, uint32_t index) {
    const data_descriptor_t *descriptor = c->data_descriptors + index;
    const gchar_t *name = (const gchar_t *)(c->data + descriptor->offset);
    return (descriptor->count == 8 && !MEMCMP(name, U"__main__", 8))
           || (descriptor->count == 7 && !MEMCMP(name, U"__dir__", 7));
}

static bool target(const bytecode_t *c, uint32_t index, const bytecode_file_t *file) {
    if (index < file->first || index - file->first >= file->count)
        return false;
    unsigned op = c->instructions[index].opcode;
    return op != ILOAD64 && op != RLOAD && op != FUNC;
}

static const bytecode_function_t *find_function(const bytecode_t *c, uint32_t id) {
    size_t lo = 0, hi = c->function_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (c->functions[mid].id < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < c->function_count && c->functions[lo].id == id ? c->functions + lo : NULL;
}

/* Call only after the location runs have been checked to partition the image. */
static const bytecode_location_t *location_at(const bytecode_t *c, uint32_t instruction) {
    size_t lo = 0, hi = c->location_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const bytecode_location_t *location = c->locations + mid;
        if (instruction < location->first)
            hi = mid;
        else if (instruction - location->first >= location->count)
            lo = mid + 1;
        else
            return location;
    }
    return NULL;
}

/* Check the producer's saved logical-selection protocol independently of runtime
 * values. The completion consumes receiver, selected function and RHS. Jumping
 * into that region would manufacture or consume another expression's selection.
 * This is validation only: executable bytes and v1 opcode numbers stay unchanged. */
static bool validate_logical_regions(const bytecode_t *c, uint32_t *region) {
    uint32_t *ends = ALLOC(c->instructions_count * sizeof(*ends));
    size_t depth = 0;
    bool valid = true;
    for (size_t i = 0; valid && i < c->instructions_count; ++i) {
        instruction_t in = c->instructions[i];
        region[i] = depth ? ends[depth - 1] : 0;
        if (in.opcode == LAND || in.opcode == LOR) {
            uint32_t end = in.arg1;
            valid = end > i + 1 && end < c->instructions_count
                    && c->instructions[end - 1].opcode == BOOL && c->instructions[end - 1].arg0 == 1
                    && (!depth || end < ends[depth - 1]);
            if (valid)
                ends[depth++] = end;
        } else if (in.opcode == BOOL && in.arg0) {
            valid = depth && ends[depth - 1] == i + 1;
            if (valid)
                --depth;
        }
    }
    valid = valid && !depth;
    for (size_t i = 0; valid && i < c->instructions_count; ++i) {
        instruction_t in = c->instructions[i];
        switch (in.opcode) {
            case JUMP:
            case JIF:
            case ITER_NEXT:
            case LAND:
            case LOR:
                valid = region[i] == region[in.arg1];
                break;
            /* Unwind transfers can leave an expression, but cannot enter a
             * saved selection that their source did not establish. */
            case LOOP_JUMP:
            case TRY:
            case FINALLY_ENTER:
            case FINALLY_LEAVE: {
                uint32_t ancestor = region[i];
                while (ancestor && ancestor != region[in.arg1])
                    ancestor = region[ancestor];
                valid = ancestor == region[in.arg1];
                break;
            }
            default:
                break;
        }
    }
    for (size_t i = 0; valid && i < c->file_count; ++i)
        valid = !region[c->files[i].entry];
    for (size_t i = 0; valid && i < c->function_count; ++i)
        valid = !region[c->functions[i].body];
    FREE(ends);
    return valid;
}

/* An instruction has one exact operand height and lexical scope chain. Scope
 * records retain their entry height so break/continue and finally exits cannot
 * erase validation information. Cleanup uses the same saved scope with a tag;
 * all potential cleanup/handler paths are checked, including exceptional ones. */
#define STACK_UNREACHED UINT64_MAX
#define CLEANUP_SCOPE UINT32_C(0x80000000)
#define SCOPE_ID(scope) ((scope) & ~CLEANUP_SCOPE)

typedef struct {
    uint64_t height;
    uint32_t scope;
} operand_state_t;

typedef struct {
    uint64_t height, floor;
    uint32_t parent;
} operand_scope_t;

static bool merge_stack(uint32_t destination,
                        uint64_t height,
                        uint32_t scope,
                        operand_state_t *states,
                        uint32_t *queue,
                        size_t *tail) {
    operand_state_t *old = states + destination;
    if (old->height != STACK_UNREACHED)
        return old->height == height && old->scope == scope;
    *old = (operand_state_t){height, scope};
    queue[(*tail)++] = destination;
    return true;
}

static bool validate_operand_stack(const bytecode_t *c, const uint32_t *region) {
    if (c->instructions_count > SIZE_MAX / sizeof(operand_state_t)
        || c->instructions_count + 1 > SIZE_MAX / sizeof(operand_scope_t))
        return false;
    operand_state_t *states = ALLOC(c->instructions_count * sizeof(*states));
    operand_scope_t *scopes = CALLOC((c->instructions_count + 1) * sizeof(*scopes));
    uint64_t *floors = CALLOC(c->instructions_count * sizeof(*floors));
    uint32_t *queue = ALLOC(c->instructions_count * sizeof(*queue));
    for (size_t i = 0; i < c->instructions_count; ++i)
        states[i].height = STACK_UNREACHED;
    size_t head = 0, tail = 0;
    bool valid = true;
    for (size_t i = 0; valid && i < c->file_count; ++i)
        valid = merge_stack(c->files[i].entry, 0, 0, states, queue, &tail);
    for (size_t i = 0; valid && i < c->function_count; ++i)
        valid = merge_stack(c->functions[i].body, 0, 0, states, queue, &tail);
    while (valid && head < tail) {
        uint32_t i = queue[head++];
        uint64_t height = states[i].height;
        uint32_t scope = states[i].scope;
        instruction_t in = c->instructions[i];
        uint64_t need = 0, pop = 0, push = 0;
        switch (in.opcode) {
            case NIL:
            case TRUE:
            case FALSE:
            case ILOAD32:
            case ILOAD64:
            case RLOAD:
            case SLOAD:
            case CLOAD:
            case VLOAD:
            case FUNC:
            case LEAVE:
            case BLOAD:
            case MODULE_INCLUDE:
            case MODULE_IMPORT:
            case MODULE_MAIN:
            case MODULE_DIR:
                push = 1;
                break;
            case POP:
            case VAR:
            case CONST:
            case DVAR:
            case DCONST:
            case JIF:
                need = pop = 1;
                break;
            case DUP:
                need = 1;
                push = 1;
                break;
            case DUPN:
                need = push = in.arg1;
                break;
            case ROTATE:
            case CHECKINDEX:
                need = (uint64_t)in.arg1 + 1;
                break;
            case STORE:
            case BSTORE:
            case GETPROP:
            case CHECKPROP:
            case UPLUS:
            case UMINUS:
            case INC:
            case DEC:
            case LNOT:
            case BNOT:
            case SNAPSHOT:
                need = 1;
                break;
            case BOOL:
                need = in.arg0 ? 3 : 1;
                pop = in.arg0 ? 2 : 0;
                break;
            case SETPROP:
                need = 2;
                pop = 1;
                break;
            case BAND:
            case BOR:
            case BXOR:
            case SHL:
            case SHR:
            case ADD:
            case SUB:
            case MUL:
            case DIVIDE:
            case MODULO:
            case POWER:
            case LESS:
            case LEQ:
            case GREATER:
            case GREQ:
            case EQUAL:
            case DIFF:
                need = 2;
                pop = 1;
                break;
            case GETINDEX:
            case SETINDEX:
                need = (uint64_t)in.arg1 + (in.opcode == SETINDEX ? 2 : 1);
                pop = need - 1;
                break;
            case ARRAY:
                need = pop = in.arg1;
                push = 1;
                break;
            case INHERIT:
                need = pop = in.arg1;
                break;
            case CALL:
            case CALLM:
                need = (uint64_t)in.arg0 + (in.opcode == CALLM ? 2 : 1);
                pop = need - 1;
                break;
            case LAND:
            case LOR:
                need = 1;
                push = 1;
                break;
            case ITER_NEXT:
                need = 1;
                push = 1;
                break;
            case RET:
            case THROW:
                need = 1;
                break;
            default:
                break;
        }
        uint64_t floor = region[i] ? floors[region[i]] : 0;
        if (in.opcode == BOOL && in.arg0) {
            /* Only the matching completion may consume the protected pair. */
            valid = floor >= 2 && height == floor + 1;
            floor -= valid ? 2 : 0;
        }
        uint64_t scope_floor = scope ? scopes[SCOPE_ID(scope)].floor : 0;
        /* ITER_NEXT only reads the snapshot retained immediately below the
         * loop boundary. All other operands must be above every saved prefix. */
        if (in.opcode == ITER_NEXT && scope && !(scope & CLEANUP_SCOPE)
            && c->instructions[scope - 1].opcode == LOOP_ENTER && height == scope_floor
            && scope_floor)
            --scope_floor;
        if (floor < scope_floor)
            floor = scope_floor;
        valid = valid && height >= floor && height - floor >= need;
        if (!valid)
            break;
        height = height - pop + push;
        bool fallthrough = true, has_branch = false;
        uint64_t branch = height;
        uint32_t branch_scope = scope;
        switch (in.opcode) {
            case ENTER:
            case INHERIT:
            case LOOP_ENTER:
                scopes[i + 1] = (operand_scope_t){height,
                                                  in.opcode == LOOP_ENTER ? height : scope_floor,
                                                  scope};
                scope = i + 1;
                break;
            case LEAVE:
            case RESTORE:
                valid = scope && !(scope & CLEANUP_SCOPE);
                if (valid)
                    scope = scopes[scope].parent;
                break;
            case END:
            case RET:
            case THROW:
                fallthrough = false;
                break;
            case FINALLY_END:
                valid = scope && (scope & CLEANUP_SCOPE);
                fallthrough = false;
                break;
            case JUMP:
                fallthrough = false;
                has_branch = true;
                break;
            case LOOP_JUMP: {
                uint32_t loop = scope;
                while (loop
                       && ((loop & CLEANUP_SCOPE)
                           || c->instructions[SCOPE_ID(loop) - 1].opcode != LOOP_ENTER))
                    loop = scopes[SCOPE_ID(loop)].parent;
                valid = loop != 0;
                if (valid) {
                    branch = scopes[loop].height;
                    branch_scope = loop;
                }
                fallthrough = false;
                has_branch = true;
                break;
            }
            case FINALLY_LEAVE:
                valid = scope && !(scope & CLEANUP_SCOPE)
                        && c->instructions[scope - 1].opcode == FINALLY_ENTER;
                if (valid) {
                    branch = scopes[scope].height;
                    branch_scope = scopes[scope].parent;
                }
                fallthrough = false;
                has_branch = true;
                break;
            case JIF:
                has_branch = true;
                break;
            case LAND:
            case LOR:
                floors[in.arg1] = height;
                has_branch = true;
                branch = height - 1;
                break;
            case ITER_NEXT:
                valid = scope && !(scope & CLEANUP_SCOPE)
                        && c->instructions[scope - 1].opcode == LOOP_ENTER;
                has_branch = true;
                branch = height - 1;
                break;
            case TRY:
            case FINALLY_ENTER:
                scopes[i + 1] = (operand_scope_t){height, height, scope};
                scope = i + 1;
                has_branch = true;
                if (in.opcode == TRY)
                    branch = height + 1;
                else
                    branch_scope = scope | CLEANUP_SCOPE;
                break;
            default:
                break;
        }
        if (valid && has_branch)
            valid = merge_stack(in.arg1, branch, branch_scope, states, queue, &tail);
        if (valid && fallthrough) {
            const bytecode_location_t *here = location_at(c, i);
            const bytecode_location_t *next = location_at(c, i + 1);
            valid = next && here->file == next->file && here->function == next->function
                    && merge_stack(i + 1, height, scope, states, queue, &tail);
        }
    }
    FREE(queue);
    FREE(floors);
    FREE(scopes);
    FREE(states);
    return valid;
}

bool validate_bytecode_image(bytecode_t *c) {
    const uint8_t *b = c->buffer;
    size_t size = c->buffer_size;
    if (!b || (uintptr_t)b % 8 || size < 64 || size > UINT32_MAX
        || memcmp(b, BINARY_FILE_SIGNATURE, 8) || read_le32(b + 8) != BYTECODE_VERSION
        || read_le32(b + 12) != 64 || read_le32(b + 16) != size)
        return false;
    uint32_t counts[5], offsets[6];
    const unsigned widths[] = {8, 16, 16, 32, 24};
    uint64_t end = 64;
    for (unsigned i = 0; i < 5; ++i) {
        counts[i] = read_le32(b + 20 + i * 4);
        offsets[i] = read_le32(b + 40 + i * 4);
        if (end != offsets[i])
            return false;
        end += (uint64_t)counts[i] * widths[i];
        if (end > size)
            return false;
    }
    offsets[5] = read_le32(b + 60);
    if (end != offsets[5] || !counts[0] || !counts[2])
        return false;
    c->instructions = (const instruction_t *)(b + offsets[0]);
    c->instructions_count = counts[0];
    c->data_descriptors = (const data_descriptor_t *)(b + offsets[1]);
    c->data_descriptor_count = counts[1];
    c->files = (const bytecode_file_t *)(b + offsets[2]);
    c->file_count = counts[2];
    c->functions = (const bytecode_function_t *)(b + offsets[3]);
    c->function_count = counts[3];
    c->locations = (const bytecode_location_t *)(b + offsets[4]);
    c->location_count = counts[4];
    c->data = b + offsets[5];
    size_t payload_size = size - offsets[5], previous = 0;
    for (size_t i = 0; i < c->data_descriptor_count; ++i) {
        data_descriptor_t d = c->data_descriptors[i];
        uint64_t aligned = ((uint64_t)previous + 7) & ~(uint64_t)7;
        if (d.offset != aligned || d.offset > payload_size || d.size > payload_size - d.offset)
            return false;
        for (size_t j = previous; j < d.offset; ++j)
            if (c->data[j])
                return false;
        const uint8_t *p = c->data + d.offset;
        switch (d.kind) {
            case DATA_STRING:
                if ((uint64_t)d.size != ((uint64_t)d.count + 1) * 4 || read_le32(p + d.size - 4))
                    return false;
                for (size_t j = 0; j < d.count; ++j)
                    if (!goat_is_scalar(read_le32(p + j * 4)))
                        return false;
                break;
            case DATA_PARAMETERS:
                if ((uint64_t)d.size != (uint64_t)d.count * 4)
                    return false;
                for (size_t j = 0; j < d.count; ++j)
                    if (!string_index(c, read_le32(p + j * 4)))
                        return false;
                break;
            case DATA_DECLARATION:
                if (d.size != 16 || d.count != 1 || read_le32(p) != DECLARATION_TAG
                    || !string_index(c, read_le32(p + 4)) || read_le32(p + 8) >= c->file_count
                    || read_le32(p + 12) > 4)
                    return false;
                break;
            default:
                return false;
        }
        previous = (size_t)d.offset + d.size;
    }
    if (previous != payload_size)
        return false;
    /* Forward references are legal: inspect names only after every referenced
       string has passed its bounds checks. Unused binding descriptors are still
       declarations and may not reserve a launch constant. */
    for (size_t i = 0; i < c->data_descriptor_count; ++i) {
        data_descriptor_t descriptor = c->data_descriptors[i];
        const uint8_t *payload = c->data + descriptor.offset;
        if (descriptor.kind == DATA_DECLARATION && launch_constant(c, read_le32(payload + 4)))
            return false;
        if (descriptor.kind == DATA_PARAMETERS) {
            for (size_t j = 0; j < descriptor.count; ++j)
                if (launch_constant(c, read_le32(payload + j * 4)))
                    return false;
        }
    }
    previous = 0;
    for (size_t i = 0; i < c->file_count; ++i) {
        const bytecode_file_t *f = c->files + i;
        if (f->first != previous || !f->count || f->count > c->instructions_count - previous
            || f->reserved || !target(c, f->entry, f))
            return false;
        previous += f->count;
    }
    if (previous != c->instructions_count)
        return false;
    size_t file_id = 0, functions = 0;
    uint8_t *declared = CALLOC(c->data_descriptor_count ? c->data_descriptor_count : 1);
    bool valid = true;
    for (size_t i = 0; valid && i < c->instructions_count; ++i) {
        while (i >= (uint64_t)c->files[file_id].first + c->files[file_id].count)
            ++file_id;
        const bytecode_file_t *file = c->files + file_id;
        instruction_t in = c->instructions[i];
        bool arg0 = in.opcode == FUNC || in.opcode == CALL || in.opcode == CALLM
                    || in.opcode == INHERIT || in.opcode == BOOL;
        bool arg1 = false;
        if (in.opcode > CHECKBOUND || in.flags || (!arg0 && in.arg0)) {
            valid = false;
            break;
        }
        switch (in.opcode) {
            case BOOL:
                valid = in.arg0 <= 1;
                break;
            case ARG:
                arg1 = true;
                valid = i + 1 < (uint64_t)file->first + file->count
                        && (c->instructions[i + 1].opcode == ILOAD64
                            || c->instructions[i + 1].opcode == RLOAD
                            || c->instructions[i + 1].opcode == FUNC);
                break;
            case ILOAD32:
            case ARRAY:
            case DUPN:
            case ROTATE:
                arg1 = true;
                break;
            case CLOAD:
                arg1 = true;
                /* Character values preserve all uint32 bits; strings require scalars. */
                break;
            case GETINDEX:
            case SETINDEX:
            case CHECKINDEX:
                arg1 = true;
                break;
            case INHERIT:
                arg1 = true;
                valid = in.arg0 <= 1 && (in.arg0 || in.arg1 == 1);
                break;
            case DVAR:
            case DCONST:
            case BLOAD:
            case BSTORE:
            case CHECKBOUND:
                arg1 = true;
                valid = in.arg1 < c->data_descriptor_count
                        && c->data_descriptors[in.arg1].kind == DATA_DECLARATION;
                if (valid && (in.opcode == DVAR || in.opcode == DCONST)) {
                    valid =
                        !declared[in.arg1]
                        && read_le32(c->data + c->data_descriptors[in.arg1].offset + 8) == file_id;
                    declared[in.arg1] = 1;
                }
                break;
            case FINALLY_ENTER:
            case FINALLY_LEAVE:
            case ITER_NEXT:
            case LOOP_JUMP:
            case JUMP:
            case JIF:
            case LAND:
            case LOR:
            case TRY:
                arg1 = true;
                valid = target(c, in.arg1, file);
                break;
            case ILOAD64:
            case RLOAD:
            case FUNC:
                arg1 = true;
                valid = i > file->first && c->instructions[i - 1].opcode == ARG;
                if (valid && in.opcode == FUNC) {
                    ++functions;
                    valid = target(c, c->instructions[i - 1].arg1, file);
                    if (in.arg0)
                        valid = valid && in.arg1 < c->data_descriptor_count
                                && c->data_descriptors[in.arg1].kind == DATA_PARAMETERS
                                && c->data_descriptors[in.arg1].count == in.arg0;
                    else
                        valid = valid && !in.arg1;
                }
                break;
            case SLOAD:
            case VLOAD:
            case VAR:
            case CONST:
            case STORE:
            case GETPROP:
            case SETPROP:
            case CHECKPROP:
            case CHECKVAR:
            case MODULE_IMPORT: /* IMPORT: execution and module semantics belong to the module
                                   loader. */
                arg1 = true;
                valid = string_index(c, in.arg1);
                if (valid
                    && (in.opcode == VAR || in.opcode == CONST || in.opcode == STORE
                        || in.opcode == CHECKVAR))
                    valid = !launch_constant(c, in.arg1);
                break;
            case MODULE_INCLUDE: /* INCLUDE */
                arg1 = true;
                valid = in.arg1 < c->file_count;
                break;
            default:
                break;
        }
        if (!arg1 && in.arg1)
            valid = false;
    }
    for (size_t i = 0; valid && i < c->instructions_count; ++i) {
        instruction_t in = c->instructions[i];
        if ((in.opcode == BLOAD || in.opcode == BSTORE || in.opcode == CHECKBOUND)
            && !declared[in.arg1])
            valid = false;
    }
    FREE(declared);
    if (!valid || functions != c->function_count)
        return false;
    uint8_t *seen = CALLOC(c->instructions_count);
    for (size_t i = 0; valid && i < c->function_count; ++i) {
        const bytecode_function_t *f = c->functions + i;
        valid = f->id && (!i || f->id > c->functions[i - 1].id) && !f->reserved
                && f->file < c->file_count && f->instruction < c->instructions_count;
        if (!valid)
            break;
        const bytecode_file_t *file = c->files + f->file;
        instruction_t in = c->instructions[f->instruction];
        valid = f->instruction > file->first && f->instruction - file->first < file->count
                && in.opcode == FUNC && !seen[f->instruction] && target(c, f->body, file)
                && c->instructions[f->instruction - 1].arg1 == f->body && f->parameters == in.arg1
                && f->parameter_count == in.arg0
                && (f->name == UINT32_MAX || string_index(c, f->name));
        seen[f->instruction] = 1;
    }
    FREE(seen);
    if (!valid)
        return false;
    previous = 0;
    for (size_t i = 0; i < c->location_count; ++i) {
        const bytecode_location_t *l = c->locations + i;
        if (l->first != previous || !l->count || l->count > c->instructions_count - previous
            || l->file >= c->file_count || (!l->line != !l->column))
            return false;
        const bytecode_file_t *f = c->files + l->file;
        if (l->first < f->first || (uint64_t)l->first + l->count > (uint64_t)f->first + f->count)
            return false;
        if (l->function) {
            const bytecode_function_t *fn = find_function(c, l->function);
            if (!fn || fn->file != l->file)
                return false;
        }
        previous += l->count;
    }
    if (previous != c->instructions_count)
        return false;
    /* A valid in-file address is not enough: file initialization must not enter
     * a function body, and each body must belong to its declared function. */
    for (size_t i = 0; i < c->file_count; ++i) {
        const bytecode_location_t *location = location_at(c, c->files[i].entry);
        if (!location || location->function)
            return false;
    }
    for (size_t i = 0; i < c->function_count; ++i) {
        const bytecode_function_t *function = c->functions + i;
        const bytecode_location_t *location = location_at(c, function->body);
        if (!location || location->function != function->id)
            return false;
    }
    /* ARG pairs and local control transfers cannot cross a top-level/function
     * boundary. Source positions may differ; code ownership may not. */
    size_t location_id = 0;
    for (size_t i = 0; i < c->instructions_count; ++i) {
        while (i - c->locations[location_id].first >= c->locations[location_id].count)
            ++location_id;
        instruction_t instruction = c->instructions[i];
        uint32_t destination;
        switch (instruction.opcode) {
            case ARG:
                destination = (uint32_t)i + 1;
                break;
            case FINALLY_ENTER:
            case FINALLY_LEAVE:
            case ITER_NEXT:
            case LOOP_JUMP:
            case JUMP:
            case JIF:
            case LAND:
            case LOR:
            case TRY:
                destination = instruction.arg1;
                break;
            default:
                continue;
        }
        const bytecode_location_t *next = location_at(c, destination);
        if (!next || next->function != c->locations[location_id].function)
            return false;
    }
    uint32_t *regions = CALLOC(c->instructions_count * sizeof(*regions));
    bool checked = validate_logical_regions(c, regions) && validate_operand_stack(c, regions);
    FREE(regions);
    return checked;
}

bytecode_t *decode_bytecode_image(const void *data, size_t size) {
    if (!data || size < 64 || size > UINT32_MAX)
        return NULL;
    bytecode_t *c = CALLOC(sizeof(*c));
    void *copy = ALLOC(size);
    memcpy(copy, data, size);
    c->buffer = copy;
    c->buffer_size = size;
    if (!validate_bytecode_image(c)) {
        free_bytecode(c);
        return NULL;
    }
    return c;
}
