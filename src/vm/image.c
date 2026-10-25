/** @file image.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared checked decoder for standalone and module v1 bytecode images.
 */
#include "bytecode.h"
#include "lib/allocate.h"
#include "lib/wire.h"

#include <float.h>
#include <string.h>

_Static_assert(sizeof(double) == 8 && FLT_RADIX == 2 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024
                   && sizeof(int64_t) == 8 && INT64_MIN == -INT64_MAX - 1,
               "binary64/int64 required");

static bool string_index(const bytecode_t *c, uint32_t index) {
    return index < c->data_descriptor_count && c->data_descriptors[index].kind == DATA_STRING;
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
        bool arg0 =
            in.opcode == FUNC || in.opcode == CALL || in.opcode == CALLM || in.opcode == INHERIT;
        bool arg1 = false;
        if (in.opcode > CHECKBOUND || in.flags || (!arg0 && in.arg0)) {
            valid = false;
            break;
        }
        switch (in.opcode) {
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
    return true;
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
