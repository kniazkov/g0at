/** @file access_check.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Shared language-level property access, independent of VM operand evaluation.
 */
#include "access.h"
#include "common_methods.h"
#include "module_files.h"

operation_result_t add_objects_with_access(process_t *process,
                                           object_t *left,
                                           object_t *right,
                                           const function_access_t *access) {
    if ((left->vtbl->type != TYPE_STRING && left->vtbl->type != TYPE_CHARACTER)
        || (right->vtbl->type != TYPE_USER_DEFINED_OBJECT && right->vtbl->type != TYPE_ARRAY))
        return add_objects(process, left, right);
    exception_t failure = object_observation_exception(right, access);
    if (failure.value) {
        INCREF(failure.value);
        return (operation_result_t){failure.value, true, failure.trace};
    }
    string_value_t text = object_to_string_with_access(right, access);
    object_t *string = create_string_object(process, text);
    operation_result_t result = add_objects(process, left, string);
    DECREF(string);
    return result;
}

static bool contains_domain(const vector_t *domains, const access_domain_t *domain) {
    for (size_t i = 0; i < domains->size; i++)
        if (domains->data[i] == domain)
            return true;
    return false;
}

bool can_access_field(const function_access_t *access, const field_cell_t *cell) {
    switch (cell->modifier) {
        case ACCESS_EXTERNAL:
            return true;
        case ACCESS_PUBLIC:
            return access->module == cell->module;
        case ACCESS_FILE:
            return access->module == cell->module && access->file_id == cell->file_id;
        case ACCESS_PRIVATE:
            return contains_domain(access->domains, cell->domain);
        case ACCESS_PROTECTED:
            return contains_domain(access->domains, cell->domain)
                   || contains_domain(access->protected_domains, cell->domain);
    }
    return false;
}

static property_lookup_t
cell_result(const function_access_t *access, object_t *storage, field_cell_t *cell) {
    object_t *failure = module_scope_failure(storage);
    if (failure)
        return (property_lookup_t){MSTAT_FAILED_FILE, storage, cell, failure};
    bool allowed = can_access_field(access, cell);
    model_status_t status = !allowed                       ? MSTAT_ACCESS_DENIED
                            : cell->pending_initialization ? MSTAT_UNINITIALIZED
                                                           : MSTAT_OK;
    return (property_lookup_t){status, storage, cell, status == MSTAT_OK ? cell->object : NULL};
}

property_lookup_t lookup_bound_property(const function_access_t *access,
                                        object_t *current,
                                        module_t *unit,
                                        uint32_t id) {
    for (size_t s = 0; s <= access->lexical_scopes->size; s++) {
        object_t *storage = s ? access->lexical_scopes->data[s - 1] : current;
        object_t *failure = module_scope_failure(storage);
        if (failure)
            return (property_lookup_t){MSTAT_FAILED_FILE, storage, NULL, failure};
        for (size_t i = 0; i < get_object_field_count(storage); i++) {
            field_cell_t *cell = get_object_field_at(storage, i);
            if (cell->binding_unit == unit && cell->declaration_id == id)
                return cell_result(access, storage, cell);
        }
        if (s == 0) {
            /* Several factory instances can share a bytecode ID after flat. Captures identify
               their concrete cells and must precede the merged storage's generic scan. */
            for (size_t c = 0; c < access->captures->size; c++) {
                field_capture_t *capture = access->captures->data[c];
                if (capture->cell->binding_unit == unit && capture->cell->declaration_id == id)
                    return cell_result(access, capture->storage, capture->cell);
            }
        }
    }
    return (property_lookup_t){.status = MSTAT_PROPERTY_NOT_FOUND};
}

property_lookup_t lookup_accessible_property(const function_access_t *access,
                                             object_t *object,
                                             object_t *key,
                                             bool private_slots) {
    object_t *failure = module_scope_failure(object);
    if (failure)
        return (property_lookup_t){MSTAT_FAILED_FILE, object, NULL, failure};
    object_array_t topology = get_object_topology(object);
    /* A granted private declaration is a slot, not a fallback after a denied ordinary lookup. */
    if (private_slots) {
        for (size_t d = 0; d < access->domains->size; d++) {
            access_domain_t *domain = access->domains->data[d];
            for (size_t i = 0; i <= topology.size; i++) {
                object_t *storage = i ? topology.items[i - 1] : object;
                field_cell_t *cell = get_domain_field_cell(storage, key, domain);
                if (cell && cell->modifier == ACCESS_PRIVATE)
                    return cell_result(access, storage, cell);
            }
        }
    }
    for (size_t i = 0; i <= topology.size; i++) {
        object_t *storage = i ? topology.items[i - 1] : object;
        failure = module_scope_failure(storage);
        if (failure)
            return (property_lookup_t){MSTAT_FAILED_FILE, storage, NULL, failure};
        field_cell_t *cell = get_own_field_cell(storage, key);
        if (cell)
            return cell_result(access, storage, cell);
        object_t *value = get_object_property(storage, key);
        if (value)
            return (property_lookup_t){MSTAT_OK, storage, NULL, value};
    }
    return (property_lookup_t){.status = MSTAT_PROPERTY_NOT_FOUND};
}

model_status_t create_accessible_property(const function_access_t *access,
                                          object_t *object,
                                          object_t *key,
                                          object_t *value,
                                          bool constant) {
    if (module_scope_failure(object))
        return MSTAT_FAILED_FILE;
    if (object->vtbl->type == TYPE_USER_DEFINED_OBJECT
        && get_object_module(object) != access->module)
        return MSTAT_FOREIGN_OBJECT;
    model_status_t status = create_object_property(object, key, value, constant);
    if (status == MSTAT_OK && object->vtbl->type == TYPE_USER_DEFINED_OBJECT) {
        field_cell_t *cell = get_own_field_cell(object, key);
        cell->file_id = access->file_id;
    }
    return status;
}

model_status_t write_accessible_property(const function_access_t *access,
                                         object_t *object,
                                         object_t *key,
                                         object_t *value) {
    property_lookup_t found = lookup_accessible_property(access, object, key, true);
    if (found.status != MSTAT_OK && found.status != MSTAT_PROPERTY_NOT_FOUND)
        return found.status;
    if (object->vtbl->type != TYPE_USER_DEFINED_OBJECT)
        return set_object_property(object, key, value);
    if (found.status == MSTAT_PROPERTY_NOT_FOUND)
        return create_accessible_property(access, object, key, value, false);
    if (!found.cell || found.cell->modifier != ACCESS_PRIVATE) {
        object_array_t topology = get_object_topology(object);
        for (size_t i = 0; i <= topology.size; i++) {
            object_t *candidate = i ? topology.items[i - 1] : object;
            if (candidate == found.storage)
                break;
            if (object_shadows_inherited(candidate))
                return create_accessible_property(access, candidate, key, value, false);
        }
    }
    if (found.cell)
        return set_field_cell_value(found.storage, found.cell, value);
    model_status_t status = set_object_property(found.storage, key, value);
    /* Existing assignment semantics allow a local declaration to shadow a built-in name. */
    if (found.storage != object && status == MSTAT_IMMUTABLE_OBJECT)
        return create_accessible_property(access, object, key, value, false);
    return status;
}

operation_result_t access_status_result(model_status_t status, object_t *value) {
    object_t *exception;
    switch (status) {
        case MSTAT_OK:
            INCREF(value);
            return operation_success(value);
        case MSTAT_FAILED_FILE:
            if (value) {
                INCREF(value);
                return operation_exception(value);
            }
            return operation_exception(get_exception_invalid_operation());
        case MSTAT_ACCESS_DENIED:
            exception = get_exception_access_denied();
            break;
        case MSTAT_UNINITIALIZED:
            exception = get_exception_uninitialized_declaration();
            break;
        case MSTAT_FOREIGN_OBJECT:
            exception = get_exception_foreign_object();
            break;
        case MSTAT_PROPERTY_IS_CONSTANT:
            exception = get_exception_property_is_constant();
            break;
        case MSTAT_PROPERTY_NOT_FOUND:
            exception = get_exception_property_not_found();
            break;
        case MSTAT_PROPERTY_ALREADY_EXISTS:
            exception = get_exception_property_already_exists();
            break;
        case MSTAT_IMMUTABLE_OBJECT:
            exception = get_exception_immutable_object();
            break;
        default:
            exception = get_exception_invalid_operation();
            break;
    }
    return operation_exception(exception);
}

operation_result_t
read_accessible_property(const function_access_t *access, object_t *object, object_t *key) {
    property_lookup_t found = lookup_accessible_property(access, object, key, true);
    if (found.status == MSTAT_PROPERTY_NOT_FOUND)
        return operation_success(get_null_object());
    operation_result_t result = access_status_result(found.status, found.value);
    if (found.status == MSTAT_FAILED_FILE)
        result.trace = module_file_exception(module_scope_file(found.storage)).trace;
    return result;
}

operation_result_t write_accessible_property_result(const function_access_t *access,
                                                    object_t *object,
                                                    object_t *key,
                                                    object_t *value) {
    property_lookup_t found = lookup_accessible_property(access, object, key, true);
    if (found.status == MSTAT_FAILED_FILE) {
        operation_result_t result = access_status_result(found.status, found.value);
        result.trace = module_file_exception(module_scope_file(found.storage)).trace;
        return result;
    }
    return access_status_result(write_accessible_property(access, object, key, value), value);
}

operation_result_t access_index(process_t *process,
                                const function_access_t *access,
                                object_t *object,
                                object_array_t indices,
                                object_t *value) {
    if (value ? object->vtbl->set_index != common_set_index
              : object->vtbl->get_index != common_get_index)
        return value ? object->vtbl->set_index(process, object, indices, value)
                     : object->vtbl->get_index(process, object, indices);
    if (indices.size != 1)
        return operation_exception(get_exception_invalid_argument());
    return value ? write_accessible_property_result(access, object, indices.items[0], value)
                 : read_accessible_property(access, object, indices.items[0]);
}
