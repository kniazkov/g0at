/** @file module_abi.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Module-v1 descriptor and invocation validation, with no VM fallback.
 */
#include "module_abi.h"

#include "lib/wire.h"
#include "module_guards.h"
#include "module_json_internal.h"

#include <string.h>

goat_module_abi_error goat_module_abi_validate(const yyjson_val *variant) {
    if (!variant)
        return GOAT_ABI_ARGUMENT;
    int64_t abi = 0;
    if (!goat_json_integer(yyjson_obj_get(variant, "goatAbiVersion"), &abi))
        return GOAT_ABI_METADATA;
    if (!goat_module_guards_validate(yyjson_obj_get(variant, "specializations")))
        return GOAT_ABI_METADATA;
    return abi == GOAT_MODULE_ABI_VERSION ? GOAT_ABI_OK : GOAT_ABI_UNSUPPORTED;
}

goat_module_abi_error
goat_module_descriptor_read(const void *bytes, size_t size, yyjson_doc **out) {
    if (!out)
        return GOAT_ABI_ARGUMENT;
    *out = NULL;
    if (!bytes || size < 17)
        return GOAT_ABI_DESCRIPTOR;
    const unsigned char *p = bytes;
    if (memcmp(p, "GOATND01", 8))
        return GOAT_ABI_DESCRIPTOR;
    uint64_t length = (uint64_t)read_le32(p + 8) | (uint64_t)read_le32(p + 12) << 32;
    if (length > SIZE_MAX - 17 || (size_t)length != size - 17 || p[size - 1])
        return GOAT_ABI_DESCRIPTOR;
    yyjson_doc *doc = NULL;
    if (goat_module_json_read((const char *)p + 16, (size_t)length, GOAT_MODULE_NATIVE, &doc))
        return GOAT_ABI_DESCRIPTOR;
    goat_module_abi_error error = GOAT_ABI_OK;
    /* The envelope promises canonical, normalized JSON, not merely equivalent JSON. */
    char *canonical = NULL;
    size_t canonical_size = 0;
    {
        if (goat_module_json_write(doc, GOAT_MODULE_NATIVE, false, &canonical, &canonical_size)
            || canonical_size != length || memcmp(canonical, p + 16, canonical_size))
            error = GOAT_ABI_DESCRIPTOR;
    }
    goat_module_json_free(canonical);
    if (error == GOAT_ABI_OK)
        error = goat_module_abi_validate(yyjson_doc_get_root(doc));
    if (error == GOAT_ABI_OK)
        *out = doc;
    else
        yyjson_doc_free(doc);
    return error;
}

static bool scalar(uint32_t value) {
    return value <= 0x10ffff && !(value >= 0xd800 && value <= 0xdfff);
}

bool goat_module_value_valid(const goat_module_value_v1 *value,
                             goat_module_readable readable,
                             void *state) {
    if (!value || value->reserved)
        return false;
    switch (value->tag) {
        case GOAT_MODULE_NULL:
            return !value->payload;
        case GOAT_MODULE_INTEGER:
        case GOAT_MODULE_REAL:
            return true;
        case GOAT_MODULE_BOOLEAN:
            return value->payload <= 1;
        case GOAT_MODULE_CHARACTER:
            return value->payload <= UINT32_MAX && scalar((uint32_t)value->payload);
        case GOAT_MODULE_STRING: {
            if (!readable || !value->payload || value->payload > UINTPTR_MAX || value->payload % 8)
                return false;
            const void *pointer = (const void *)(uintptr_t)value->payload;
            if (!readable(state, pointer, sizeof(goat_module_string_v1)))
                return false;
            goat_module_string_v1 view;
            memcpy(&view, pointer, sizeof(view));
#if UINTPTR_MAX == UINT32_MAX
            if (view.reserved)
                return false;
#endif
            if (view.length > SIZE_MAX / sizeof(uint32_t) || (!view.data && view.length))
                return false;
            if (view.length
                && (!readable(state, view.data, (size_t)view.length * sizeof(uint32_t))
                    || (uintptr_t)view.data % _Alignof(uint32_t)))
                return false;
            for (size_t i = 0; i < (size_t)view.length; i++)
                if (!scalar(view.data[i]))
                    return false;
            return true;
        }
        default:
            return false;
    }
}

bool goat_module_context_valid(const goat_module_context_v1 *context) {
    if (!context || !context->services)
        return false;
    const goat_module_services_v1 *s = context->services;
    return s->struct_size >= GOAT_MODULE_SERVICES_SIZE && !s->reserved && s->allocate
           && s->deallocate && s->deallocate_all && s->is_space && s->to_upper && s->to_lower;
}

goat_module_abi_error goat_module_abi_invoke(goat_module_adapter_v1 adapter,
                                             uint32_t expected_result,
                                             const goat_module_context_v1 *context,
                                             uint32_t count,
                                             const goat_module_value_v1 *arguments,
                                             goat_module_value_v1 *result,
                                             goat_module_readable readable,
                                             void *state,
                                             uint32_t *native_status) {
    if (native_status)
        *native_status = UINT32_MAX;
    if (!result)
        return GOAT_ABI_ARGUMENT;
    memset(result, 0, sizeof(*result));
    if (!adapter || expected_result > GOAT_MODULE_CHARACTER || (count && !arguments))
        return GOAT_ABI_ARGUMENT;
#if SIZE_MAX <= UINT32_MAX
    if (count > SIZE_MAX / sizeof(*arguments))
        return GOAT_ABI_ARGUMENT;
#endif
    if (!goat_module_context_valid(context))
        return GOAT_ABI_CONTEXT;
    for (uint32_t i = 0; i < count; i++)
        if (!goat_module_value_valid(arguments + i, readable, state))
            return GOAT_ABI_VALUE;
    uint32_t status = adapter(GOAT_MODULE_ABI_VERSION, context, count, arguments, result);
    if (native_status)
        *native_status = status;
    goat_module_abi_error error = GOAT_ABI_OK;
    if (status != GOAT_MODULE_SUCCESS)
        error = GOAT_ABI_CALL_FAILED;
    else if (result->tag != expected_result || !goat_module_value_valid(result, readable, state))
        error = GOAT_ABI_VALUE;
    if (error)
        memset(result, 0, sizeof(*result));
    return error;
}
