/** @file test_binary.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Checked decoding, corruption rejection and command modes.
 */
#include "test_binary.h"

#include "cli/options.h"
#include "codegen/linker.h"
#include "lib/allocate.h"
#include "lib/binary_file.h"
#include "test_macro.h"
#include "vm/binary.h"

#include <stdio.h>
#include <string.h>

static const char *filename = "goat-binary-unit.gbin";

static bytecode_t *sample(void) {
    code_builder_t *code = create_code_builder();
    data_builder_t *data = create_data_builder();
    uint32_t id = add_string_to_data_segment(data, L"Unicode: \u03bb\u0416");
    add_instruction(code, (instruction_t){.opcode = SLOAD, .arg1 = id});
    add_instruction(code, (instruction_t){.opcode = POP});
    add_instruction(code, (instruction_t){.opcode = END});
    bytecode_t *result = link_code_and_data(code, data);
    destroy_code_builder(code);
    destroy_data_builder(data);
    return result;
}

bool test_sha256(void) {
    const char *inputs[] = {"", "abc", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
    const char *expected[] = {"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"};
    for (size_t i = 0; i < 4; i++) {
        char *large = i == 3 ? ALLOC(1000000) : NULL;
        if (large)
            memset(large, 'a', 1000000);
        uint8_t digest[SHA256_SIZE];
        sha256(large ? large : inputs[i], large ? 1000000 : strlen(inputs[i]), digest);
        FREE(large);
        char hex[65];
        for (size_t j = 0; j < SHA256_SIZE; j++)
            sprintf(hex + 2 * j, "%02x", (unsigned)digest[j]);
        ASSERT(!strcmp(hex,
                       i == 3 ? "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"
                              : expected[i]));
    }
    return true;
}

bool test_binary_roundtrip(void) {
    bytecode_t *code = sample();
    ASSERT(save_binary_program(filename, code, 0));
    binary_program_t loaded = load_binary_program(filename);
    ASSERT(loaded.code && !loaded.binding_count);
    ASSERT(loaded.code->buffer_size == code->buffer_size);
    ASSERT(!memcmp(loaded.code->buffer, code->buffer, code->buffer_size));
    ASSERT(loaded.code->instructions_count == 3 && loaded.code->data_descriptor_count == 1);
    ASSERT(!loaded.code->native_functions);
    ASSERT(!wcscmp((const wchar_t *)loaded.code->data, L"Unicode: \u03bb\u0416"));
    destroy_binary_program(&loaded);
    free_bytecode(code);
    ASSERT(!remove(filename));
    return true;
}

static void put64(unsigned char *bytes, size_t offset, uint64_t value) {
    for (unsigned i = 0; i < 8; i++)
        bytes[offset + i] = (unsigned char)(value >> (8 * i));
}

static bool rejected(const void *bytes, size_t size) {
    binary_program_t loaded = decode_binary_program(bytes, size);
    bool result = !loaded.code;
    if (!result)
        printf("Decoder accepted a corrupted binary (%zu bytes)\n", size);
    destroy_binary_program(&loaded);
    return result;
}

bool test_binary_rejection(void) {
    bytecode_t *code = sample();
    ASSERT(save_binary_program(filename, code, 0));
    size_t size;
    unsigned char *bytes = read_binary_file(filename, GOAT_BINARY_LIMIT, &size);
    ASSERT(bytes);
    uint64_t checksum = binary_checksum(bytes, size);
    binary_program_t loaded = decode_binary_program(bytes, size);
    ASSERT(loaded.code);
    ASSERT(!memcmp(loaded.code->buffer, code->buffer, code->buffer_size));
    ASSERT(binary_checksum(bytes, size) == checksum);
    destroy_binary_program(&loaded);
    ASSERT(rejected(NULL, size));
    ASSERT(rejected(bytes, (size_t)GOAT_BINARY_LIMIT + 1));
    for (size_t length = 0; length < size; length++)
        ASSERT(rejected(bytes, length));
    unsigned char *copy = ALLOC(size + 1);
    for (size_t offset = 0; offset < size; offset++) {
        memcpy(copy, bytes, size);
        copy[offset] ^= 0x80;
        bool invalid = rejected(copy, size);
        if (!invalid)
            printf("Corrupted byte offset: %zu\n", offset);
        ASSERT(invalid);
    }
    memcpy(copy, bytes, size);
    copy[size] = 0;
    ASSERT(rejected(copy, size + 1));
    /* A valid old checksum cannot make wrapping-era bytecode compatible. */
    memcpy(copy, bytes, size);
    memcpy(copy, "GOATBIN2", 8);
    put64(copy, 8, 2);
    put64(copy, 56, binary_checksum(copy, size));
    ASSERT(rejected(copy, size));
    /* Recompute checksums to exercise structural validation independently. */
    const size_t offsets[] = {8,
                              16,
                              24,
                              32,
                              40,
                              48,
                              64,
                              96 + 8,
                              96 + 16,
                              96 + 24,
                              96 + sizeof(goat_binary_header_t),
                              96 + sizeof(goat_binary_header_t) + 4,
                              96 + sizeof(goat_binary_header_t) + 3 * sizeof(instruction_t)};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(*offsets); i++) {
        memcpy(copy, bytes, size);
        put64(copy, offsets[i], UINT64_MAX);
        put64(copy, 56, 0);
        put64(copy, 56, binary_checksum(copy, size));
        ASSERT(rejected(copy, size));
    }
    memcpy(copy, bytes, size);
    copy[size - sizeof(wchar_t)] = 1; /* Missing string terminator. */
    put64(copy, 56, 0);
    put64(copy, 56, binary_checksum(copy, size));
    ASSERT(rejected(copy, size));
    FREE(copy);
    FREE(bytes);
    free_bytecode(code);
    ASSERT(!remove(filename));
    return true;
}

bool test_binary_options(void) {
    char *compile[] = {"goat", "--compile", "--optimize", "none", "program.goat"};
    options_t *options = parse_options(5, compile);
    ASSERT(options && options->compile_only && !options->run_binary
           && options->native_execution == NATIVE_OFF);
    destroy_options(options);
    char *run[] = {"goat", "--run", "program.gbin"};
    options = parse_options(3, run);
    ASSERT(options && options->run_binary && !options->compile_only
           && options->native_execution == NATIVE_AUTO);
    destroy_options(options);
    char *off[] = {"goat", "--run", "--native", "off", "program.gbin"};
    options = parse_options(5, off);
    ASSERT(options && options->native_execution == NATIVE_OFF);
    destroy_options(options);
    return true;
}
