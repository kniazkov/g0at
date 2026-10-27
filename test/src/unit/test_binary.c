/** @file test_binary.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Checked decoding, corruption rejection and command modes.
 */
#include "test_binary.h"

#include "cli/options.h"
#include "codegen/linker.h"
#include "lib/allocate.h"
#include "lib/binary_file.h"
#include "model/access.h"
#include "test_macro.h"
#include "vm/binary.h"
#include "vm/vm.h"

#include <stdio.h>
#include <string.h>

static const char *filename = "goat-binary-unit.gbin";

static bytecode_t *sample(void) {
    code_builder_t *code = create_code_builder();
    data_builder_t *data = create_data_builder();
    uint32_t id = add_string_to_data_segment(data, U"Unicode: \u03bb\u0416");
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
        for (size_t chunk = 1; chunk <= 129; chunk += 8) {
            sha256_context ctx;
            sha256_init(&ctx);
            const char *data = large ? large : inputs[i];
            size_t length = large ? 1000000 : strlen(inputs[i]);
            sha256_update(&ctx, NULL, 0);
            for (size_t offset = 0; offset < length;) {
                size_t count = length - offset;
                if (count > chunk)
                    count = chunk;
                sha256_update(&ctx, data + offset, count);
                offset += count;
            }
            uint8_t streamed[SHA256_SIZE];
            sha256_finish(&ctx, streamed);
            ASSERT(!memcmp(digest, streamed, SHA256_SIZE));
        }
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
    void *snapshot = ALLOC(code->buffer_size);
    memcpy(snapshot, loaded.code->buffer, code->buffer_size);
    ASSERT(validate_bytecode_image(loaded.code));
    for (unsigned i = 0; i < 2; ++i) {
        process_t *process = create_process();
        ASSERT(!run(process, loaded.code));
        destroy_process(process);
        ASSERT(!memcmp(snapshot, loaded.code->buffer, code->buffer_size));
    }
    FREE(snapshot);
    ASSERT(!STRCMP((const gchar_t *)loaded.code->data, U"Unicode: \u03bb\u0416"));
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

/** @brief Check declaration metadata independently of the outer checksum. */
static bool declaration_rejection(void) {
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    uint32_t name = add_string_to_data_segment(data, U"field");
    declaration_descriptor_t declaration = {DECLARATION_TAG, name, 0, ACCESS_PRIVATE};
    uint32_t id = add_data_to_data_segment(data, &declaration, sizeof(declaration));
    data->descriptors[id].kind = DATA_DECLARATION;
    data->descriptors[id].count = 1;
    add_instruction(builder, (instruction_t){.opcode = DVAR, .arg1 = id});
    add_instruction(builder, (instruction_t){.opcode = BLOAD, .arg1 = id});
    add_instruction(builder, (instruction_t){.opcode = END});
    bytecode_t *code = link_code_and_data(builder, data);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    instruction_t original = ((instruction_t *)code->instructions)[0];
    data_descriptor_t descriptor = ((data_descriptor_t *)code->data_descriptors)[id];
    for (unsigned test = 0; test < 13; test++) {
        declaration_descriptor_t altered = declaration;
        ((instruction_t *)code->instructions)[0] = original;
        ((instruction_t *)code->instructions)[1].opcode = BLOAD;
        ((data_descriptor_t *)code->data_descriptors)[id] = descriptor;
        switch (test) {
            case 1:
                altered.tag = 0;
                break;
            case 2:
                altered.name = UINT32_MAX;
                break;
            case 3:
                altered.file = 1;
                break;
            case 4:
                altered.modifier = 99;
                break;
            case 5:
                altered.modifier = 5;
                break;
            case 6:
                ((data_descriptor_t *)code->data_descriptors)[id].size--;
                break;
            case 7:
                ((instruction_t *)code->instructions)[0].arg1 = UINT32_MAX;
                break;
            case 8:
                ((instruction_t *)code->instructions)[0].opcode = BLOAD;
                break; /* No declaration. */
            case 9:
                ((instruction_t *)code->instructions)[0].arg0 = 1;
                break;
            case 10:
                ((instruction_t *)code->instructions)[1].opcode =
                    DCONST; /* Duplicate declaration identity. */
                break;
            case 12:
                altered.modifier = ACCESS_FILE;
                break;
            case 11:
                altered.name = id; /* Metadata is not a name string. */
                break;
        }
        memcpy((uint8_t *)code->data + descriptor.offset, &altered, sizeof(altered));
        ASSERT(save_binary_program(filename, code, NULL));
        binary_program_t loaded = load_binary_program(filename);
        ASSERT((loaded.code != NULL) == (test == 0 || test == 12));
        destroy_binary_program(&loaded);
    }
    free_bytecode(code);
    ASSERT(!remove(filename));
    return true;
}

/** @brief Validate unreachable code and entry ownership without executing any instruction. */
static bool instruction_rejection(void) {
    code_builder_t *builder = create_code_builder();
    data_builder_t *data = create_data_builder();
    add_instruction(builder, (instruction_t){.opcode = END});
    add_instruction(builder, (instruction_t){.opcode = ARG, .arg1 = 8});
    add_instruction(builder, (instruction_t){.opcode = FUNC});
    builder->function_ids[2] = 1;
    add_instruction(builder, (instruction_t){.opcode = POP});
    add_instruction(builder, (instruction_t){.opcode = ARG, .arg1 = 10});
    add_instruction(builder, (instruction_t){.opcode = FUNC});
    builder->function_ids[5] = UINT32_MAX;
    add_instruction(builder, (instruction_t){.opcode = POP});
    add_instruction(builder, (instruction_t){.opcode = END});
    builder->location.function = 1;
    add_instruction(builder, (instruction_t){.opcode = NIL});
    add_instruction(builder, (instruction_t){.opcode = RET});
    builder->location.function = UINT32_MAX;
    add_instruction(builder, (instruction_t){.opcode = NIL});
    add_instruction(builder, (instruction_t){.opcode = RET});
    bytecode_t *code = link_code_and_data(builder, data);
    destroy_code_builder(builder);
    destroy_data_builder(data);
    /* UINT32_MAX and sparse IDs are valid identities, not table indices. */
    bytecode_t *loaded = decode_bytecode_image(code->buffer, code->buffer_size);
    ASSERT(loaded);
    free_bytecode(loaded);
    void *snapshot = ALLOC(code->buffer_size);
    memcpy(snapshot, code->buffer, code->buffer_size);
    for (unsigned test = 0; test < 15; ++test) {
        memcpy((void *)code->buffer, snapshot, code->buffer_size);
        instruction_t *instructions = (instruction_t *)code->instructions;
        bytecode_function_t *functions = (bytecode_function_t *)code->functions;
        bytecode_location_t *locations = (bytecode_location_t *)code->locations;
        switch (test) {
            case 0:
                ((bytecode_file_t *)code->files)[0].entry = 8;
                break; /* A valid address, but a function body instead of top-level code. */
            case 1:
                functions[0].body = instructions[1].arg1 = 0;
                break; /* A function cannot start in top-level code. */
            case 2:
                functions[0].body = instructions[1].arg1 = 10;
                break; /* Nor in another function's body. */
            case 3:
                locations[0].count = 2;
                locations[1].first = 2;
                locations[1].count = 8;
                break; /* ARG/consumer must retain the same code owner. */
            case 4:
                instructions[3].opcode = 255;
                break;
            case 5:
                instructions[3].flags = 1;
                break;
            case 6:
                instructions[3].arg0 = 1;
                break;
            case 7:
                instructions[3].arg1 = 1;
                break;
            case 8:
                instructions[3] = (instruction_t){.opcode = JUMP, .arg1 = 12};
                break;
            case 9:
                instructions[3] = (instruction_t){.opcode = TRY, .arg1 = 5};
                break; /* No branch or structured-control target may skip ARG. */
            case 10:
                instructions[1].opcode = NOP;
                instructions[1].arg1 = 0;
                break;
            case 11:
                instructions[3] = (instruction_t){.opcode = SLOAD};
                break;
            case 12:
                instructions[3] = (instruction_t){.opcode = MODULE_INCLUDE, .arg1 = 1};
                break;
            case 13:
                functions[1].id = functions[0].id;
                break;
            case 14:
                locations[2].function = 2;
                break;
        }
        ASSERT(rejected(code->buffer, code->buffer_size));
    }
    const opcode_t branches[] =
        {JUMP, JIF, LAND, LOR, TRY, LOOP_JUMP, ITER_NEXT, FINALLY_ENTER, FINALLY_LEAVE};
    for (size_t i = 0; i < sizeof(branches) / sizeof(*branches); ++i) {
        memcpy((void *)code->buffer, snapshot, code->buffer_size);
        instruction_t *instructions = (instruction_t *)code->instructions;
        instructions[8] = (instruction_t){.opcode = branches[i], .arg1 = 0};
        ASSERT(rejected(code->buffer, code->buffer_size)); /* Function to top-level. */
        instructions[8].arg1 = 10;
        ASSERT(rejected(code->buffer, code->buffer_size)); /* Between functions. */
        instructions[8].arg1 = 9;
        bytecode_t *valid = decode_bytecode_image(code->buffer, code->buffer_size);
        ASSERT(valid); /* A control target within the same function is allowed. */
        free_bytecode(valid);
        instructions[3] = (instruction_t){.opcode = branches[i], .arg1 = 8};
        ASSERT(rejected(code->buffer, code->buffer_size)); /* Top-level to function. */
    }
    memcpy((void *)code->buffer, snapshot, code->buffer_size);
    ASSERT(validate_bytecode_image(code));
    ASSERT(!memcmp(code->buffer, snapshot, code->buffer_size));
    FREE(snapshot);
    free_bytecode(code);
    return true;
}

bool test_binary_rejection(void) {
    ASSERT(declaration_rejection());
    ASSERT(instruction_rejection());
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
    /* Reject earlier instruction sets even when the checksum is valid. */
    for (unsigned version = 1; version < 11; version++) {
        memcpy(copy, bytes, size);
        memcpy(copy, "GOATBIN", 7);
        copy[7] = (unsigned char)('0' + version);
        if (version == 10)
            memcpy(copy, "GOATBI10", 8);
        put64(copy, 8, version);
        put64(copy, 56, 0);
        put64(copy, 56, binary_checksum(copy, size));
        ASSERT(rejected(copy, size));
    }
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
    copy[size - sizeof(gchar_t)] = 1; /* Missing string terminator. */
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
