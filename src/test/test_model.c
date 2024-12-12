/**
 * @file test_model.c
 * @copyright 2025 Ivan Kniazkov
 * @brief A set of tests for testing object model.
 */

#include <stdio.h>

#include "test_model.h"
#include "test_macro.h"
#include "model/object.h"
#include "model/process.h"
#include "lib/allocate.h"

bool test_integer_object() {
    process_t *proc = create_process();
    object_t *obj = object_integer_create(proc, -1024);
    wchar_t *str = obj->vtbl->to_string(obj);
    ASSERT(wcscmp(L"-1024", str) == 0);
    FREE(str);
    destroy_process(proc);
    return true;
}
