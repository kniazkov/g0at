/** @file native_artifact_validation.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Reuse the runtime descriptor contract for static build-time artifact validation.
 */
#include "native_artifact_validation.h"

#include "lib/memory/allocate.h"
#include "native_artifact.h"
#include "vm/module_consistency.h"

#include <string.h>

static bool fail(char **diagnostic, const char *message) {
    FREE(*diagnostic);
    size_t length = strlen(message) + 1;
    *diagnostic = ALLOC(length);
    memcpy(*diagnostic, message, length);
    return false;
}

static bool validate(native_artifact_t *artifact,
                     const yyjson_doc *metadata,
                     size_t variant_index,
                     char **diagnostic) {
    if (!artifact)
        return false;
    const yyjson_val *root = metadata ? yyjson_doc_get_root((yyjson_doc *)metadata) : NULL;
    const yyjson_val *variant = yyjson_arr_get(yyjson_obj_get(root, "native"), variant_index);
    if (!variant) {
        close_native_artifact(artifact);
        return fail(diagnostic, "Invalid native artifact validation arguments");
    }
    const void *bytes = NULL;
    size_t size = 0;
    bool ok = native_artifact_descriptor(artifact, &bytes, &size, diagnostic);
    if (ok && match_module_descriptor_metadata(root, variant, bytes, size) != MODULE_CONSISTENCY_OK)
        ok = fail(diagnostic, "Produced native descriptor does not match module metadata");
    const yyjson_val *specializations = yyjson_obj_get(variant, "specializations");
    for (size_t i = 0; ok && i < yyjson_arr_size(specializations); i++) {
        const char *symbol =
            yyjson_get_str(yyjson_obj_get(yyjson_arr_get(specializations, i), "symbol"));
        if (!symbol || !native_artifact_has_function(artifact, symbol))
            ok = fail(diagnostic,
                      "Produced native library does not export a declared adapter function");
    }
    close_native_artifact(artifact);
    return ok;
}

bool validate_native_artifact(const char *path,
                              const yyjson_doc *metadata,
                              size_t variant_index,
                              char **diagnostic) {
    if (!diagnostic)
        return false;
    *diagnostic = NULL;
    return validate(open_native_artifact(path, diagnostic), metadata, variant_index, diagnostic);
}

bool validate_native_artifact_bytes(const void *bytes,
                                    size_t size,
                                    const yyjson_doc *metadata,
                                    size_t variant_index,
                                    char **diagnostic) {
    if (!diagnostic)
        return false;
    *diagnostic = NULL;
    return validate(open_native_artifact_bytes(bytes, size, diagnostic),
                    metadata,
                    variant_index,
                    diagnostic);
}
