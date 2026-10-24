/** @file zip_archive.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Bounded in-memory ZIP infrastructure, independent of the VM.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ZIP_SUCCESS,
    ZIP_INVALID,
    ZIP_MEMORY,
    ZIP_FORMAT,
    ZIP_ZIP64,
    ZIP_MULTIDISK,
    ZIP_UNSUPPORTED,
    ZIP_CAPACITY,
    ZIP_SIZE,
    ZIP_CRC,
    ZIP_DEFLATE,
    ZIP_HEADER_MISMATCH
} zip_status_t;

const char *zip_status_string(zip_status_t status);

typedef struct {
    const uint8_t *data;
    size_t size;
} zip_view_t;
typedef struct zip_archive zip_archive_t;

/** Raw central record. Names/comments/extras are bounded bytes, never truncated
 * or deduplicated. Views borrow the original archive. Offsets are absolute except
 * declared_local_offset. Interpretation of paths, link attributes and extras is
 * the caller's responsibility. */
typedef struct {
    zip_view_t header, name, extra, comment;
    size_t central_offset, local_offset;
    uint32_t declared_local_offset, compressed_size, size, crc32, external_attributes;
    uint16_t made_by, needed, flags, method, time, date, disk, internal_attributes;
} zip_entry_t;

typedef struct {
    zip_view_t header, name, extra;
    size_t offset, data_offset;
    uint32_t compressed_size, size, crc32;
    uint16_t needed, flags, method, time, date;
} zip_local_t;

typedef struct {
    zip_view_t bytes, end_record, comment;
    size_t central_offset, central_size, prefix_size, trailing_size;
    uint32_t declared_central_offset;
    size_t count;
} zip_layout_t;

/** Borrow immutable input until close. No filesystem access. ZIP64/multidisk
 * produce distinct errors, never silently downgraded. Failure clears *out.
 * This is structural indexing, not validation of the Goat container policy. */
zip_status_t zip_archive_open(const void *data, size_t size, zip_archive_t **out);
void zip_archive_close(zip_archive_t *archive);
zip_status_t zip_archive_layout(const zip_archive_t *archive, zip_layout_t *out);
zip_status_t zip_archive_entry(const zip_archive_t *archive, size_t index, zip_entry_t *out);
zip_status_t zip_archive_local(const zip_archive_t *archive, size_t index, zip_local_t *out);
/** No allocations proportional to expanded size. Never writes beyond capacity.
 * On failure *written=0, but buffer may contain partial untrusted output.
 * Verify sizes, complete Deflate consumption, CRC and local/central agreement.
 * Data descriptors remain exposed in raw bytes for the container validator. */
zip_status_t zip_archive_read(const zip_archive_t *archive,
                              size_t index,
                              void *buffer,
                              size_t capacity,
                              size_t *written);

/** All envelope metadata is explicit. flags may be 0 or UTF-8 (0x0800);
 * method is 0 (stored) or 8 (Deflate). No automatic stored fallback.
 * Names must be nonempty, NUL-free byte strings; comments are arbitrary bytes; extras must be
 * complete ZIP TLV records without ZIP64 (tag 1). Order and duplicates are preserved. No path
 * interpretation occurs.
 */
typedef struct {
    zip_view_t name, data, local_extra, central_extra, comment;
    uint16_t method, flags, time, date, made_by, internal_attributes;
    uint32_t external_attributes;
} zip_write_entry_t;

/** Owned output, freed with zip_archive_free. All failures clear output/size.
 * max_size caps allocation and writing (also below ZIP32 sentinels).
 * Raw Deflate: zlib 1.3.2, level 9, windowBits=-15, memLevel=8, default strategy,
 * one final flush. Version-needed=20, no descriptor or automatic ZIP64.
 * No filesystem publication or Goat-specific entry naming/order is imposed.
 */
zip_status_t zip_archive_write(const zip_write_entry_t *entries,
                               size_t count,
                               zip_view_t comment,
                               size_t max_size,
                               void **out,
                               size_t *size);
void zip_archive_free(void *data);
