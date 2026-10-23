/** @file linker.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Builds the fixed v1 memory image before publishing immutable views.
 */
#include "linker.h"

#include "lib/allocate.h"
#include "lib/wire.h"

#include <stdlib.h>
#include <string.h>

static int compare_functions(const void *a, const void *b) {
    uint32_t x = ((const bytecode_function_t *)a)->id;
    uint32_t y = ((const bytecode_function_t *)b)->id;
    return x < y ? -1 : x > y;
}

static bool same_location(bytecode_location_t a, bytecode_location_t b) {
    return a.file == b.file && a.line == b.line && a.column == b.column && a.function == b.function;
}

bytecode_t *link_code_and_data(code_builder_t *cb, data_builder_t *db) {
    size_t functions = 0;
    for (size_t i = 0; i < cb->size; ++i)
        functions += cb->instructions[i].opcode == FUNC;
    size_t locations = 0;
    for (size_t i = 0; i < cb->size; ++i)
        locations += !i || !same_location(cb->locations[i - 1], cb->locations[i]);
    uint64_t od = 64 + (uint64_t)cb->size * 8;
    uint64_t of = od + (uint64_t)db->descriptors_count * 16;
    uint64_t on = of + 16;
    uint64_t ol = on + (uint64_t)functions * 32;
    uint64_t payload = ol + (uint64_t)locations * 24;
    uint64_t total = payload + db->data_size;
    if (total > UINT32_MAX || total > SIZE_MAX)
        abort();
    bytecode_function_t *records = CALLOC((functions ? functions : 1) * sizeof(*records));
    uint8_t *bytes = CALLOC((size_t)total);
    memcpy(bytes, BINARY_FILE_SIGNATURE, 8);
    uint32_t header[] = {1,
                         64,
                         (uint32_t)total,
                         (uint32_t)cb->size,
                         (uint32_t)db->descriptors_count,
                         1,
                         (uint32_t)functions,
                         (uint32_t)locations,
                         64,
                         (uint32_t)od,
                         (uint32_t)of,
                         (uint32_t)on,
                         (uint32_t)ol,
                         (uint32_t)payload};
    for (size_t i = 0; i < 14; ++i)
        write_le32(bytes + 8 + i * 4, header[i]);
    size_t function = 0;
    for (size_t i = 0; i < cb->size; ++i) {
        instruction_t in = cb->instructions[i];
        uint8_t *p = bytes + 64 + i * 8;
        p[0] = in.opcode;
        p[1] = in.flags;
        p[2] = (uint8_t)in.arg0;
        p[3] = (uint8_t)(in.arg0 >> 8);
        write_le32(p + 4, in.arg1);
        if (in.opcode == FUNC) {
            records[function] = (bytecode_function_t){cb->function_ids[i] ? cb->function_ids[i]
                                                                          : (uint32_t)function + 1,
                                                      0,
                                                      (uint32_t)i,
                                                      i ? cb->instructions[i - 1].arg1 : 0,
                                                      in.arg1,
                                                      in.arg0,
                                                      UINT32_MAX,
                                                      0};
            ++function;
        }
    }
    qsort(records, functions, sizeof(*records), compare_functions);
    for (size_t i = 0; i < functions; ++i) {
        const bytecode_function_t *f = records + i;
        const uint32_t words[] = {f->id,
                                  f->file,
                                  f->instruction,
                                  f->body,
                                  f->parameters,
                                  f->parameter_count,
                                  f->name,
                                  f->reserved};
        for (size_t j = 0; j < 8; ++j)
            write_le32(bytes + on + i * 32 + j * 4, words[j]);
    }
    FREE(records);
    size_t location = 0;
    for (size_t first = 0; first < cb->size;) {
        size_t next = first + 1;
        while (next < cb->size && same_location(cb->locations[first], cb->locations[next]))
            ++next;
        bytecode_location_t l = cb->locations[first];
        const uint32_t words[] =
            {(uint32_t)first, (uint32_t)(next - first), l.file, l.line, l.column, l.function};
        for (size_t j = 0; j < 6; ++j)
            write_le32(bytes + ol + location * 24 + j * 4, words[j]);
        ++location;
        first = next;
    }
    for (size_t i = 0; i < db->descriptors_count; ++i) {
        data_descriptor_t d = db->descriptors[i];
        write_le32(bytes + od + i * 16, d.offset);
        write_le32(bytes + od + i * 16 + 4, d.size);
        write_le32(bytes + od + i * 16 + 8, d.kind);
        write_le32(bytes + od + i * 16 + 12, d.count);
        /* Strings already contain explicit LE scalars; other blocks contain u32 words. */
        if (d.kind == DATA_STRING)
            memcpy(bytes + payload + d.offset, db->data + d.offset, d.size);
        else
            for (size_t j = 0; j < d.size; j += 4) {
                uint32_t word;
                memcpy(&word, db->data + d.offset + j, 4);
                write_le32(bytes + payload + d.offset + j, word);
            }
    }
    write_le32(bytes + of + 4, (uint32_t)cb->size);
    bytecode_t *code = CALLOC(sizeof(*code));
    code->buffer = bytes;
    code->buffer_size = (size_t)total;
    code->instructions = (const instruction_t *)(bytes + 64);
    code->instructions_count = cb->size;
    code->data_descriptors = (const data_descriptor_t *)(bytes + od);
    code->data_descriptor_count = db->descriptors_count;
    code->data = bytes + payload;
    code->files = (const bytecode_file_t *)(bytes + of);
    code->file_count = 1;
    code->functions = (const bytecode_function_t *)(bytes + on);
    code->function_count = functions;
    code->locations = (const bytecode_location_t *)(bytes + ol);
    code->location_count = locations;
    return code;
}
