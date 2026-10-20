/** @file access.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Lifetime of module/domain identities and captured field storage.
 */
#include "access.h"

#include "lib/allocate.h"

#include <assert.h>

/** @brief Module identity shared by objects, declarations and functions via retained pointers. */
struct module_t {
    size_t refs;
};

/** @brief Declaration identity that can outlive its original object through fields or grants. */
struct access_domain_t {
    size_t refs;
    module_t *module;
};

/** @brief Immortal built-in module; retain/release never modify its reference count. */
static module_t builtin_module;

module_t *create_module_identity(void) {
    module_t *module = ALLOC(sizeof(*module));
    module->refs = 1;
    return module;
}

module_t *get_builtin_module_identity(void) {
    return &builtin_module;
}

module_t *retain_module_identity(module_t *module) {
    assert(module);
    if (module != &builtin_module)
        module->refs++;
    return module;
}

void release_module_identity(module_t *module) {
    if (module && module != &builtin_module && !--module->refs)
        FREE(module);
}

access_domain_t *create_access_domain(module_t *module) {
    access_domain_t *domain = ALLOC(sizeof(*domain));
    domain->refs = 1;
    domain->module = retain_module_identity(module);
    return domain;
}

access_domain_t *retain_access_domain(access_domain_t *domain) {
    assert(domain);
    domain->refs++;
    return domain;
}

void release_access_domain(access_domain_t *domain) {
    if (domain && !--domain->refs) {
        release_module_identity(domain->module);
        FREE(domain);
    }
}

module_t *get_access_domain_module(const access_domain_t *domain) {
    return domain->module;
}

void init_function_access(function_access_t *access, module_t *module) {
    access->module = retain_module_identity(module);
    access->domains = create_vector();
    access->captures = create_vector();
}

void add_function_access_domain(function_access_t *access, access_domain_t *domain) {
    for (size_t i = 0; i < access->domains->size; i++)
        if (access->domains->data[i] == domain)
            return;
    append_to_vector(access->domains, retain_access_domain(domain));
}

bool add_function_field_capture(function_access_t *access, object_t *storage, field_cell_t *cell) {
    bool found = false;
    for (size_t i = 0; i < get_object_field_count(storage); i++)
        if (get_object_field_at(storage, i) == cell) {
            found = true;
            break;
        }
    if (!found)
        return false;
    for (size_t i = 0; i < access->captures->size; i++) {
        field_capture_t *capture = access->captures->data[i];
        if (capture->storage == storage && capture->cell == cell)
            return true;
    }
    field_capture_t *capture = ALLOC(sizeof(*capture));
    *capture = (field_capture_t){storage, cell};
    INCREF(storage);
    append_to_vector(access->captures, capture);
    return true;
}

void mark_function_access(const function_access_t *access) {
    for (size_t i = 0; i < access->captures->size; i++) {
        field_capture_t *capture = access->captures->data[i];
        mark_object(capture->storage);
    }
}

void clear_function_access(function_access_t *access, bool deep) {
    for (size_t i = 0; i < access->captures->size; i++) {
        field_capture_t *capture = access->captures->data[i];
        if (deep)
            DECREF(capture->storage);
        FREE(capture);
    }
    for (size_t i = 0; i < access->domains->size; i++)
        release_access_domain(access->domains->data[i]);
    destroy_vector(access->captures);
    destroy_vector(access->domains);
    release_module_identity(access->module);
}
