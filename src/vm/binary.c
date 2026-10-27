/** @file binary.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Checked bytecode decoding; no pointers or compiler state are persisted.
 */
#include "binary.h"

#include "codegen/native_compiler.h"
#include "lib/files/binary_file.h"
#include "lib/memory/allocate.h"
#include "model/native_library.h"

#include <float.h>
#include <string.h>

#define HEADER_SIZE 96
#define FORMAT_VERSION 1

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
    memcpy(bytes, "GOATPG01", 8);
    write64(bytes + 8, FORMAT_VERSION);
    write64(bytes + 16, 0);
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
    if (bytes && size >= 64 && size <= GOAT_BINARY_LIMIT
        && !memcmp(bytes, BINARY_FILE_SIGNATURE, 8)) {
        result.code = decode_bytecode_image(bytes, size);
        return result;
    }
    if (!bytes || size < HEADER_SIZE || size > GOAT_BINARY_LIMIT)
        goto done;
    uint64_t expected = read64(bytes + 56);
    const uint8_t zero_checksum[8] = {0};
    uint64_t checksum = binary_checksum(bytes, 56);
    checksum = extend_binary_checksum(checksum, zero_checksum, sizeof(zero_checksum));
    checksum = extend_binary_checksum(checksum, bytes + 64, size - 64);
    uint64_t code_size = read64(bytes + 24), count = read64(bytes + 32);
    if (memcmp(bytes, "GOATPG01", 8) || read64(bytes + 8) != FORMAT_VERSION
        || read64(bytes + 16) != 0 || read64(bytes + 40) || read64(bytes + 48)
        || expected != checksum || code_size > size - HEADER_SIZE
        || count > (size - HEADER_SIZE - code_size) / 16
        || size != HEADER_SIZE + code_size + count * 16)
        goto done;
    result.code = decode_bytecode_image(bytes + HEADER_SIZE, (size_t)code_size);
    if (!result.code)
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
        size_t lo = 0, hi = result.code->function_count;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (result.code->functions[mid].id < id)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo == result.code->function_count || result.code->functions[lo].id != id
            || result.code->functions[lo].instruction != index)
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
            const goat_native_entry_v2_t *entry = get_native_function_entry(function, j);
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
