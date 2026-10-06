/** @file binary.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Checked bytecode decoding; no pointers or compiler state are persisted.
 */
#include "binary.h"

#include "codegen/native_compiler.h"
#include "lib/allocate.h"
#include "lib/binary_file.h"
#include "model/native_library.h"

#include <float.h>
#include <string.h>

#define HEADER_SIZE 96
#define FORMAT_VERSION 3

static uint64_t read64(const uint8_t *p) {
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; i++)
        value |= (uint64_t)p[i] << (8 * i);
    return value;
}

static void write64(uint8_t *p, uint64_t value) {
    for (unsigned i = 0; i < 8; i++)
        p[i] = (uint8_t)(value >> (8 * i));
}

static uint64_t platform(void) {
    uint32_t endian = 1;
    return sizeof(wchar_t) | (uint64_t) * (uint8_t *)&endian << 8;
}

static bool string_valid(const bytecode_t *code, uint32_t index) {
    if (index >= code->data_descriptor_count)
        return false;
    data_descriptor_t d = code->data_descriptors[index];
    if (d.size < sizeof(wchar_t) || d.size % sizeof(wchar_t) || d.offset % sizeof(wchar_t))
        return false;
    const wchar_t *text = (const wchar_t *)(code->data + d.offset);
    size_t length = d.size / sizeof(wchar_t);
    return !text[length - 1] && !wmemchr(text, 0, length - 1);
}

/** @brief Validates offsets before forming pointers, then all instruction data references. */
static bool decode(bytecode_t *code) {
    if (sizeof(double) != 8 || FLT_RADIX != 2 || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024
        || code->buffer_size < sizeof(goat_binary_header_t))
        return false;
    goat_binary_header_t h;
    memcpy(&h, code->buffer, sizeof(h));
    if (memcmp(h.signature, BINARY_FILE_SIGNATURE, 8) || h.instructions_offset != sizeof(h)
        || h.data_descriptors_offset < sizeof(h) || h.data_offset < h.data_descriptors_offset
        || h.data_offset > code->buffer_size
        || (h.data_descriptors_offset - sizeof(h)) % sizeof(instruction_t)
        || (h.data_offset - h.data_descriptors_offset) % sizeof(data_descriptor_t))
        return false;
    code->instructions = (instruction_t *)((uint8_t *)code->buffer + sizeof(h));
    code->instructions_count = (h.data_descriptors_offset - sizeof(h)) / sizeof(instruction_t);
    code->data_descriptors =
        (data_descriptor_t *)((uint8_t *)code->buffer + h.data_descriptors_offset);
    code->data_descriptor_count =
        (h.data_offset - h.data_descriptors_offset) / sizeof(data_descriptor_t);
    code->data = (uint8_t *)code->buffer + h.data_offset;
    if (!code->instructions_count)
        return false;
    size_t size = code->buffer_size - h.data_offset;
    for (size_t i = 0; i < code->data_descriptor_count; i++) {
        data_descriptor_t d = code->data_descriptors[i];
        if (d.offset > size || d.size > size - d.offset || d.offset % 4)
            return false;
    }
    for (size_t i = 0; i < code->instructions_count; i++) {
        instruction_t in = code->instructions[i];
        if (in.opcode > THROW || in.flags)
            return false;
        switch (in.opcode) {
            case JUMP:
            case JIF:
            case LAND:
            case LOR:
            case TRY:
                if (in.arg1 >= code->instructions_count)
                    return false;
                break;
            case ILOAD64:
            case RLOAD:
            case FUNC:
                if (!i || code->instructions[i - 1].opcode != ARG)
                    return false;
                if (in.opcode != FUNC)
                    break;
                if (code->instructions[i - 1].arg1 >= code->instructions_count)
                    return false;
                if (in.arg0) {
                    if (in.arg1 >= code->data_descriptor_count)
                        return false;
                    data_descriptor_t d = code->data_descriptors[in.arg1];
                    if (d.size != (size_t)in.arg0 * sizeof(uint32_t))
                        return false;
                    const uint32_t *names = (const uint32_t *)(code->data + d.offset);
                    for (size_t j = 0; j < in.arg0; j++)
                        if (!string_valid(code, names[j]))
                            return false;
                }
                break;
            case SLOAD:
            case VLOAD:
            case VAR:
            case CONST:
            case STORE:
                if (!string_valid(code, in.arg1))
                    return false;
                break;
            default:
                break;
        }
    }
    return true;
}

bool save_binary_program(const char *path,
                         const bytecode_t *code,
                         const uint8_t library_digest[SHA256_SIZE]) {
    size_t count = 0;
    for (size_t i = 0; i < code->instructions_count; i++)
        if (get_bytecode_native_function(code, i))
            count++;
    if (count && !library_digest)
        return false;
    if (code->buffer_size > GOAT_BINARY_LIMIT - HEADER_SIZE
        || count > (GOAT_BINARY_LIMIT - HEADER_SIZE - code->buffer_size) / 16)
        return false;
    size_t size = HEADER_SIZE + code->buffer_size + count * 16;
    uint8_t *bytes = CALLOC(size);
    memcpy(bytes, "GOATBIN3", 8);
    write64(bytes + 8, FORMAT_VERSION);
    write64(bytes + 16, platform());
    write64(bytes + 24, code->buffer_size);
    write64(bytes + 32, count);
    if (count)
        memcpy(bytes + 64, library_digest, SHA256_SIZE);
    memcpy(bytes + HEADER_SIZE, code->buffer, code->buffer_size);
    uint8_t *binding = bytes + HEADER_SIZE + code->buffer_size;
    for (size_t i = 0; i < code->instructions_count; i++) {
        native_function_descriptor_t *function = get_bytecode_native_function(code, i);
        if (!function)
            continue;
        write64(binding, i);
        write64(binding + 8, get_native_function_entry(function, 0)->function_id);
        binding += 16;
    }
    write64(bytes + 56, binary_checksum(bytes, size));
    bool ok = write_binary_file(path, bytes, size);
    FREE(bytes);
    return ok;
}

binary_program_t load_binary_program(const char *path) {
    size_t size;
    uint8_t *bytes = read_binary_file(path, GOAT_BINARY_LIMIT, &size);
    binary_program_t result = decode_binary_program(bytes, size);
    FREE(bytes);
    return result;
}

binary_program_t decode_binary_program(const void *data, size_t size) {
    binary_program_t result = {0};
    const uint8_t *bytes = data;
    if (!bytes || size < HEADER_SIZE || size > GOAT_BINARY_LIMIT)
        goto done;
    uint64_t expected = read64(bytes + 56);
    const uint8_t zero_checksum[8] = {0};
    uint64_t checksum = binary_checksum(bytes, 56);
    checksum = extend_binary_checksum(checksum, zero_checksum, sizeof(zero_checksum));
    checksum = extend_binary_checksum(checksum, bytes + 64, size - 64);
    uint64_t code_size = read64(bytes + 24), count = read64(bytes + 32);
    if (memcmp(bytes, "GOATBIN3", 8) || read64(bytes + 8) != FORMAT_VERSION
        || read64(bytes + 16) != platform() || read64(bytes + 40) || read64(bytes + 48)
        || expected != checksum || code_size > size - HEADER_SIZE
        || count > (size - HEADER_SIZE - code_size) / 16
        || size != HEADER_SIZE + code_size + count * 16)
        goto done;
    result.code = CALLOC(sizeof(bytecode_t));
    result.code->buffer_size = (size_t)code_size;
    result.code->buffer = ALLOC(code_size ? (size_t)code_size : 1);
    memcpy(result.code->buffer, bytes + HEADER_SIZE, code_size);
    if (!decode(result.code))
        goto invalid;
    result.binding_count = (size_t)count;
    memcpy(result.library_digest, bytes + 64, SHA256_SIZE);
    const uint8_t empty_digest[SHA256_SIZE] = {0};
    if (!count && memcmp(result.library_digest, empty_digest, SHA256_SIZE))
        goto invalid;
    result.bindings = count ? ALLOC((size_t)count * 2 * sizeof(uint64_t)) : NULL;
    const uint8_t *binding = bytes + HEADER_SIZE + code_size;
    for (size_t i = 0; i < count; i++, binding += 16) {
        uint64_t index = read64(binding), id = read64(binding + 8);
        if (index >= result.code->instructions_count || !id
            || result.code->instructions[index].opcode != FUNC
            || (i && index <= result.bindings[2 * (i - 1)]))
            goto invalid;
        result.bindings[2 * i] = index;
        result.bindings[2 * i + 1] = id;
    }
    goto done;
invalid:
    destroy_binary_program(&result);
done:
    return result;
}

static void cleanup_library_copy(void *workspace) {
    destroy_native_workspace(workspace);
}

bool bind_binary_library(binary_program_t *program, const char *path) {
    if (!program->code || !program->binding_count || program->code->native_functions)
        return false;
    size_t size;
    void *bytes = read_binary_file(path, GOAT_BINARY_LIMIT, &size);
    uint8_t digest[SHA256_SIZE] = {0};
    if (bytes)
        sha256(bytes, size, digest);
    bool matches = bytes && !memcmp(digest, program->library_digest, SHA256_SIZE);
    native_workspace_t *workspace = matches ? create_native_workspace() : NULL;
    bool copied = workspace && write_binary_file(workspace->library, bytes, size);
    FREE(bytes);
    if (!copied) {
        destroy_native_workspace(workspace);
        return false;
    }
    /* Load precisely the bytes checked above, even if the companion is replaced meanwhile. */
    native_library_result_t loaded = load_native_library(workspace->library);
    if (loaded.library) {
        set_native_library_cleanup(loaded.library, workspace, cleanup_library_copy);
        workspace = NULL;
    }
    destroy_native_workspace(workspace);
    bool ok = loaded.status == NATIVE_LIBRARY_OK;
    for (size_t i = 0; ok && i < program->binding_count; i++) {
        size_t index = program->bindings[2 * i];
        native_function_descriptor_t *function =
            create_native_function_descriptor(loaded.library, program->bindings[2 * i + 1]);
        ok = function != NULL;
        for (uint32_t j = 0; ok && j < get_native_function_entry_count(function); j++) {
            const goat_native_entry_v1_t *entry = get_native_function_entry(function, j);
            ok = entry->flags == GOAT_NATIVE_PURE && !entry->binding_name
                 && entry->parameter_count == program->code->instructions[index].arg0;
        }
        if (ok)
            ok = bind_bytecode_native_function(program->code, index, function);
        release_native_function_descriptor(function);
    }
    if (!ok)
        for (size_t i = 0; i < program->binding_count; i++)
            bind_bytecode_native_function(program->code, program->bindings[2 * i], NULL);
    destroy_native_library_result(&loaded);
    return ok;
}

void destroy_binary_program(binary_program_t *program) {
    if (program->code)
        free_bytecode(program->code);
    FREE(program->bindings);
    *program = (binary_program_t){0};
}
