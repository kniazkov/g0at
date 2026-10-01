/** @file test_exceptions.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Built-in exception constants, lookup and lifetime.
 */
#include <stdio.h>
#include "test_macro.h"
#include "model/object.h"
#include "model/context.h"
#include "model/process.h"
#include "lib/allocate.h"

bool test_exceptions_object(void) {
    object_t *exceptions = get_exceptions_object();
    ASSERT(get_object_property(get_root_context()->data, get_string_exceptions()) == exceptions);
    const wchar_t *names[] = {L"DIVISION_BY_ZERO", L"IMMUTABLE_OBJECT", L"INVALID_ARGUMENT",
        L"INVALID_OPERATION", L"PROPERTY_ALREADY_EXISTS", L"PROPERTY_IS_CONSTANT",
        L"PROPERTY_NOT_FOUND"};
    object_t *values[] = {get_exception_division_by_zero(), get_exception_immutable_object(),
        get_exception_invalid_argument(), get_exception_invalid_operation(),
        get_exception_property_already_exists(), get_exception_property_is_constant(),
        get_exception_property_not_found()};
    object_array_t keys = get_object_keys(exceptions);
    ASSERT(keys.size == sizeof(names) / sizeof(*names));
    process_t *process = create_process();
    for (size_t i = 0; i < keys.size; i++) {
        /* Separately allocated keys must work by content, not singleton identity. */
        object_t *key = create_string_object(process,
            (string_value_t){names[i], wcslen(names[i]), false});
        object_t *value = get_object_property(exceptions, key);
        ASSERT(value == values[i]);
        ASSERT(value->vtbl->type == TYPE_STRING);
        string_value_t text = convert_object_to_string(value);
        ASSERT(text.length == wcslen(names[i]) && !wmemcmp(text.data, names[i], text.length));
        FREE_STRING(text);
        ASSERT(get_object_property(exceptions, keys.items[i]) == value);
        ASSERT(set_object_property(exceptions, key, get_null_object()) == MSTAT_IMMUTABLE_OBJECT);
        ASSERT(create_object_property(exceptions, key, get_null_object(), false) == MSTAT_IMMUTABLE_OBJECT);
        ASSERT(get_object_property(exceptions, key) == value);
        INCREF(value); DECREF(value);
        value->vtbl->mark(value);
        ASSERT(!value->vtbl->sweep(value));
        object_t *copy = value->vtbl->clone(process, value);
        ASSERT(copy->vtbl->compare(copy, value) == 0);
        DECREF(copy);
        DECREF(key);
    }
    ASSERT(!get_object_property(exceptions, get_null_object()));
    ASSERT(!get_object_property(exceptions, get_static_integer_object(1)));
    ASSERT(!get_object_property(exceptions, get_empty_string()));
    ASSERT(!get_object_property(exceptions, get_string_print()));
    ASSERT(create_object_property(exceptions, get_string_print(), get_null_object(), true)
        == MSTAT_IMMUTABLE_OBJECT);
    ASSERT(exceptions->vtbl->clone(process, exceptions) == exceptions);
    INCREF(exceptions); DECREF(exceptions);
    exceptions->vtbl->mark(exceptions);
    ASSERT(!exceptions->vtbl->sweep(exceptions));
    object_array_t root_keys = get_object_keys(get_root_context()->data);
    size_t found = 0;
    for (size_t i = 0; i < root_keys.size; i++) {
        if (root_keys.items[i] == get_string_exceptions()) found++;
        ASSERT(get_object_property(get_root_context()->data, root_keys.items[i]));
    }
    ASSERT(found == 1);
    ASSERT(set_object_property(get_root_context()->data, get_string_exceptions(), get_null_object())
        == MSTAT_IMMUTABLE_OBJECT);
    destroy_process(process);
    ASSERT(get_object_property(exceptions, get_exception_invalid_argument()) == values[2]);
    return true;
}
