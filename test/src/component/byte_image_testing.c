/** @file byte_image_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent BYTELOAD image, ownership and corruption controls.
 */
#ifdef NDEBUG
#    undef NDEBUG
#endif
#include "codegen/linker.h"
#include "lib/memory/allocate.h"
#include "model/byte_array.h"
#include "model/thread.h"
#include "vm/vm.h"

#include <assert.h>
#include <string.h>

/* Hand-authored wire v1: BYTELOAD 0; THROW; END. One three-byte descriptor,
 * one file and one source-location run. No production serializer generated it. */
static const uint8_t golden[147] = {
    [0] = 'G', 'O',        'A',        'T',        'B',        'C',       '0',       '1',
    [8] = 1,   [12] = 64,  [16] = 147, [20] = 3,   [24] = 1,   [28] = 1,  [36] = 1,  [40] = 64,
    [44] = 88, [48] = 104, [52] = 120, [56] = 120, [60] = 144, [64] = 81, [72] = 55, [80] = 2,
    [92] = 3,  [96] = 4,   [100] = 3,  [108] = 3,  [124] = 3,  [144] = 0, 255,       128};

int main(void) {
    size_t baseline = get_allocated_memory_size();
    bytecode_t *image = decode_bytecode_image(golden, sizeof(golden));
    assert(image);
    code_builder_t *cb = create_code_builder();
    data_builder_t *db = create_data_builder();
    add_bytes_to_data_segment(db, golden + 144, 3);
    add_instruction(cb, (instruction_t){.opcode = BYTELOAD});
    add_instruction(cb, (instruction_t){.opcode = THROW});
    add_instruction(cb, (instruction_t){.opcode = END});
    bytecode_t *built = link_code_and_data(cb, db);
    assert(built && built->buffer_size == sizeof(golden));
    assert(!memcmp(built->buffer, golden, sizeof(golden)));
    free_bytecode(built);
    destroy_code_builder(cb);
    destroy_data_builder(db);
    for (size_t size = 0; size < sizeof(golden); ++size)
        assert(!decode_bytecode_image(golden, size));
    const size_t corrupt[] = {64, 65, 66, 68, 88, 92, 96, 100};
    for (size_t i = 0; i < sizeof(corrupt) / sizeof(*corrupt); ++i) {
        uint8_t bytes[sizeof(golden)];
        memcpy(bytes, golden, sizeof(bytes));
        bytes[corrupt[i]] = 255;
        assert(!decode_bytecode_image(bytes, sizeof(bytes)));
    }
    process_t *processes[2];
    object_t *values[2];
    for (size_t i = 0; i < 2; ++i) {
        processes[i] = create_process();
        bytecode_t *wrapper = borrow_bytecode_image(image);
        assert(wrapper && wrapper->buffer == image->buffer);
        assert(run(processes[i], wrapper) != 0); /* THROW exports the result to the host. */
        values[i] = processes[i]->main_thread->exception.value;
        byte_buffer_t *buffer = get_byte_array_storage(values[i]);
        assert(buffer && buffer->length == 3 && !memcmp(buffer->data, golden + 144, 3));
        assert(buffer->data != image->data);
        buffer->data[0] = (uint8_t)(i + 1);
        assert(!memcmp(image->buffer, golden, sizeof(golden)));
        free_bytecode(wrapper);
    }
    free_bytecode(image);
    for (size_t i = 0; i < 2; ++i) {
        byte_buffer_t *buffer = get_byte_array_storage(values[i]);
        assert(buffer->data[0] == i + 1 && buffer->data[1] == 255);
        assert(byte_buffer_resize(buffer, 100, 42));
        assert(buffer->data[99] == 42);
        destroy_process(processes[i]);
    }
    assert(get_allocated_memory_size() == baseline);
    return 0;
}
