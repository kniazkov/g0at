/** @file format_zip.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent ZIP fixtures, using only zlib primitives, never Goat wrappers.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const unsigned char *name, *data, *extra, *comment;
    size_t name_size, data_size, extra_size, comment_size;
    uint16_t method, flags, date, time, made_by, internal;
    uint32_t external;
} format_zip_entry_t;

/** Golden-profile defaults (UTF-8, 1980-01-01, Unix regular 0644). */
void format_zip_entry_init(format_zip_entry_t *entry,
                           const char *name,
                           const void *data,
                           size_t size,
                           bool compressed);
/** Input order and duplicate names are retained. Result is malloc-owned.
 * streamed adds signed data descriptors; extras/comments may contain NUL.
 */
unsigned char *format_zip_build(const format_zip_entry_t *entries,
                                size_t count,
                                const void *comment,
                                size_t comment_size,
                                bool streamed,
                                size_t *size,
                                char *error,
                                size_t error_size);

typedef struct {
    size_t count, directory_offset, central_offset, local_offset, data_offset, end_offset;
    const unsigned char *name, *extra, *comment, *archive_comment;
    size_t name_size, extra_size, comment_size, archive_comment_size;
    uint16_t method, flags, date, time, made_by, internal;
    uint32_t external, crc, packed_size, size;
} format_zip_info_t;

/** Inspect a central-directory entry without discarding duplicate or binary names.
 * index==SIZE_MAX returns archive-only information, including an empty archive.
 * Pointers borrow the input. Extraction additionally verifies the local header.
 */
bool format_zip_inspect(const unsigned char *data,
                        size_t size,
                        size_t index,
                        format_zip_info_t *info,
                        char *error,
                        size_t error_size);
/** Strict stored/raw-deflate extraction, checking CRC, sizes and local fields.
 * max_size bounds allocation. Empty entries still return a malloc-owned pointer.
 */
unsigned char *format_zip_extract(const unsigned char *data,
                                  size_t size,
                                  size_t index,
                                  size_t max_size,
                                  size_t *output_size,
                                  char *error,
                                  size_t error_size);
/** Bounded byte mutation for intentionally malformed fixture headers (1/2/4/8 bytes). */
bool format_bytes_patch_le(unsigned char *data,
                           size_t size,
                           size_t offset,
                           uint64_t value,
                           unsigned width);

/** Copy a byte sequence, replacing a bounded range; NULL on invalid input/overflow.
 * The malloc-owned result can be empty. ZIP offsets are deliberately NOT repaired:
 * use patch_le for valid relocations or retain damaged fields for negative fixtures.
 * Insert may alias input. Input is unchanged.
 */
unsigned char *format_bytes_splice(const unsigned char *data,
                                   size_t size,
                                   size_t offset,
                                   size_t remove_size,
                                   const void *insert,
                                   size_t insert_size,
                                   size_t *output_size);
