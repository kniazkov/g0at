#include "module_hash.h"

#include "../lib/sha256.h"

#include <string.h>

static void hash_length(sha256_context *ctx, uint64_t length) {
    uint8_t bytes[8];
    for (unsigned i = 0; i < 8; i++)
        bytes[i] = (uint8_t)(length >> (8 * i));
    sha256_update(ctx, bytes, sizeof(bytes));
}

goat_module_json_error goat_module_main_hash(const yyjson_doc *metadata,
                                             const void *bytecode,
                                             size_t length,
                                             char out[GOAT_MODULE_HASH_HEX_SIZE]) {
    if (out)
        memset(out, 0, GOAT_MODULE_HASH_HEX_SIZE);
    if (!out || (!bytecode && length))
        return GOAT_MODULE_JSON_INVALID_FIELD;
    /* SHA-256 uses a 64-bit bit count. Check before accessing the byte buffer. */
    const uint64_t limit = UINT64_MAX / 8;
    const uint64_t framing = 18 + 8 + 8;
    if (length > limit - framing)
        return GOAT_MODULE_JSON_LIMIT;
    char *canonical = NULL;
    size_t canonical_length = 0;
    goat_module_json_error err =
        goat_module_json_write(metadata, GOAT_MODULE_METADATA, true, &canonical, &canonical_length);
    if (err)
        return err;
    if (canonical_length > limit - framing - length) {
        goat_module_json_free(canonical);
        return GOAT_MODULE_JSON_LIMIT;
    }
    /* sizeof includes the NUL following MAIN; version is a separate byte. */
    static const char marker[] = "GOAT-MODULE-MAIN";
    const uint8_t version = 1;
    sha256_context ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, marker, sizeof(marker));
    sha256_update(&ctx, &version, 1);
    hash_length(&ctx, canonical_length);
    sha256_update(&ctx, canonical, canonical_length);
    hash_length(&ctx, length);
    sha256_update(&ctx, bytecode, length);
    goat_module_json_free(canonical);
    uint8_t digest[SHA256_SIZE];
    sha256_finish(&ctx, digest);
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < SHA256_SIZE; i++) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 15];
    }
    return GOAT_MODULE_JSON_OK;
}

goat_module_json_error goat_module_main_hash_verify(const yyjson_doc *metadata,
                                                    const void *bytecode,
                                                    size_t length,
                                                    bool *matches) {
    if (!matches)
        return GOAT_MODULE_JSON_INVALID_FIELD;
    *matches = false;
    char actual[GOAT_MODULE_HASH_HEX_SIZE];
    goat_module_json_error err = goat_module_main_hash(metadata, bytecode, length, actual);
    if (!err) {
        yyjson_val *expected = yyjson_obj_get(yyjson_doc_get_root(metadata), "mainHash");
        *matches = !memcmp(actual, yyjson_get_str(expected), GOAT_MODULE_HASH_HEX_SIZE - 1);
    }
    return err;
}
