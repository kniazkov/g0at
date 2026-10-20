/** @file access.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Internal declaration identities and stable field captures; no access enforcement yet.
 */
#pragma once

#include "lib/vector.h"
#include "object.h"

typedef struct module_identity_t module_identity_t;
typedef struct access_domain_t access_domain_t;
typedef struct field_cell_t field_cell_t;

/** @brief Declaration visibility, independent of the constant flag. */
typedef enum { ACCESS_MODULE, ACCESS_PUBLIC, ACCESS_PROTECTED, ACCESS_PRIVATE } access_modifier_t;

/** @brief Stable cell owned by one storage object; all pointers are borrowed by readers.
 * Keys and declaration metadata are immutable after insertion. Change values through the API.
 */
struct field_cell_t {
    object_t *key;
    object_t *object;
    bool is_constant;
    access_modifier_t modifier;
    module_identity_t *module;
    access_domain_t *domain;
};

/** @brief Retains storage, keeping its cell alive. Does not own the cell independently. */
typedef struct {
    object_t *storage;
    field_cell_t *cell;
} field_capture_t;

/** @brief Function metadata. Captures and domains are populated before publication. */
typedef struct {
    module_identity_t *module;
    vector_t *domains;
    vector_t *captures;
} function_access_t;

/** @brief Creates a distinct module identity with one owned reference. */
module_identity_t *create_module_identity(void);
/** @brief Immutable immortal identity for built-in declarations. */
module_identity_t *get_builtin_module_identity(void);
module_identity_t *retain_module_identity(module_identity_t *module);
void release_module_identity(module_identity_t *module);
/** @brief A domain is an identity, not a reference back to its originating object. */
access_domain_t *create_access_domain(module_identity_t *module);
access_domain_t *retain_access_domain(access_domain_t *domain);
void release_access_domain(access_domain_t *domain);
module_identity_t *get_access_domain_module(const access_domain_t *domain);

/** @brief Creates a user object in an explicit module, with a fresh declaration domain. */
object_t *create_user_defined_object_in_module(process_t *process,
                                               object_array_t prototypes,
                                               module_identity_t *module);
module_identity_t *get_object_module(const object_t *obj);
access_domain_t *get_object_access_domain(const object_t *obj);
/** @brief Full internal cell inventory, including cells omitted from ordinary lookup. */
size_t get_object_field_count(const object_t *obj);
field_cell_t *get_object_field_at(const object_t *obj, size_t index);
field_cell_t *get_own_field_cell(const object_t *obj, const object_t *key);
field_cell_t *
get_domain_field_cell(const object_t *obj, const object_t *key, const access_domain_t *domain);
/** @brief Adds an internal cell. Domain/key pairs must be unique; selected keys must be unique.
 * A NULL result means a duplicate or invalid implicit-this declaration. Metadata is retained.
 */
field_cell_t *create_object_field(object_t *obj,
                                  object_t *key,
                                  object_t *value,
                                  bool constant,
                                  access_modifier_t modifier,
                                  module_identity_t *module,
                                  access_domain_t *domain,
                                  bool selected);
/** @brief Writes an owned cell, respecting constness but not visibility (stage B). */
model_status_t set_field_cell_value(object_t *storage, field_cell_t *cell, object_t *value);
/** @brief Metadata of an existing own property, not an inherited lookup or an access check.
 * Built-in properties are public; user fields carry their explicit modifier.
 */
access_modifier_t get_object_property_modifier(const object_t *obj, const object_t *key);

/** @brief Initializes owned metadata; no captures or domain grants initially. */
void init_function_access(function_access_t *access, module_identity_t *module);
void add_function_access_domain(function_access_t *access, access_domain_t *domain);
/** @brief Adds one stable capture, validating that storage owns the cell. */
bool add_function_field_capture(function_access_t *access, object_t *storage, field_cell_t *cell);
void mark_function_access(const function_access_t *access);
/** @brief deep=false frees metadata without dereferencing GC-managed objects during sweep. */
void clear_function_access(function_access_t *access, bool deep);
/** @brief Dynamic function metadata, or NULL for built-in/static functions. */
function_access_t *get_function_access(object_t *function);
