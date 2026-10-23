/** @file access.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Declaration identities, stable field captures and language-level access policy.
 */
#pragma once

#include "lib/vector.h"
#include "object.h"

typedef struct module_t module_t;
typedef struct access_domain_t access_domain_t;
typedef struct field_cell_t field_cell_t;

/** @brief Declaration visibility, independent of the constant flag. */
typedef enum {
    ACCESS_MODULE = 0,
    ACCESS_PUBLIC = 1,
    ACCESS_PROTECTED = 3,
    ACCESS_PRIVATE = 4
} access_modifier_t;

/** @brief Stable cell owned by one storage object; all pointers are borrowed by readers.
 * Keys and declaration metadata are immutable after insertion. Change values through the API.
 */
struct field_cell_t {
    object_t *key;
    object_t *object;
    bool is_constant;
    access_modifier_t modifier;
    /** @brief Declaring module, independent of the storage object's owning module. */
    module_t *module;
    /** @brief Distinguishes same-key declarations belonging to different objects. */
    access_domain_t *domain;
    module_t *binding_unit;  /**< Owned load identity; never persisted as a pointer. */
    uint32_t declaration_id; /**< File-local descriptor index plus one; zero means dynamic. */
};

/** @brief Retains storage, keeping its cell alive. Does not own the cell independently. */
typedef struct {
    object_t *storage;
    field_cell_t *cell;
} field_capture_t;

/** @brief Function/context snapshot. Captures and domains are populated before publication. */
typedef struct {
    module_t *module;
    /** @brief Retained access_domain_t pointers, nearest lexical domain first, without duplicates.
     */
    vector_t *domains;
    /** @brief Retained domains whose protected fields are accessible through inheritance. */
    vector_t *protected_domains;
    /** @brief Owned field_capture_t records retaining their storage objects. */
    vector_t *captures;
    vector_t *lexical_scopes; /**< Retained lexical storage, excluding dynamic receivers. */
} function_access_t;

/** @brief Formats containers recursively using the same observer at every depth.
 * A NULL observer is reserved for trusted internal formatting (no filtering).
 */
string_value_t object_to_string_with_access(const object_t *obj, const function_access_t *access);
/** @brief Snapshots accessible own lookup keys; never traverses prototypes or hidden cells. */
object_t *snapshot_object_keys_with_access(process_t *process,
                                           object_t *obj,
                                           const function_access_t *access);
/** @brief Applies caller visibility to implicit string/character concatenation. */
operation_result_t add_objects_with_access(process_t *process,
                                           object_t *left,
                                           object_t *right,
                                           const function_access_t *access);

/** @brief Creates a distinct module identity with one owned reference. */
module_t *create_module_identity(void);
/** @brief Immutable immortal identity for built-in declarations. */
module_t *get_builtin_module_identity(void);
module_t *retain_module_identity(module_t *module);
void release_module_identity(module_t *module);
/** @brief A domain is an identity, not a reference back to its originating object. */
access_domain_t *create_access_domain(module_t *module);
access_domain_t *retain_access_domain(access_domain_t *domain);
void release_access_domain(access_domain_t *domain);
module_t *get_access_domain_module(const access_domain_t *domain);

/** @brief Creates a user object in an explicit module, with a fresh declaration domain. */
object_t *create_user_defined_object_in_module(process_t *process,
                                               object_array_t prototypes,
                                               module_t *module);
/** @brief Borrows the object's owning module, or the declaring module for a function. */
module_t *get_object_module(const object_t *obj);
access_domain_t *get_object_access_domain(const object_t *obj);
/** @brief Full internal cell inventory, including cells omitted from ordinary lookup. */
size_t get_object_field_count(const object_t *obj);
field_cell_t *get_object_field_at(const object_t *obj, size_t index);
/** @brief Borrows the own lookup winner; does not search prototypes or hidden cells. */
field_cell_t *get_own_field_cell(const object_t *obj, const object_t *key);
/** @brief Borrows an own cell by declaration domain, including cells hidden from lookup. */
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
                                  module_t *module,
                                  access_domain_t *domain,
                                  bool selected);
/** @brief Trusted storage write: respects constness; language callers must check access first. */
model_status_t set_field_cell_value(object_t *storage, field_cell_t *cell, object_t *value);
/** @brief Metadata of an existing own property, not an inherited lookup or an access check.
 * Built-in properties are public; user fields carry their explicit modifier.
 */
access_modifier_t get_object_property_modifier(const object_t *obj, const object_t *key);

/** @brief Initializes owned metadata; no captures or domain grants initially. */
void init_function_access(function_access_t *access, module_t *module);
void add_function_access_domain(function_access_t *access, access_domain_t *domain);
/** @brief Copies grants and captures into an unpublished context in the same module. */
void inherit_function_access(function_access_t *target, const function_access_t *source);
/** @brief Retains lexical storage, including fields declared after closure creation. */
void add_lexical_scope(function_access_t *access, object_t *storage);
/** @brief Grants protected access to an object's declaration domain and its prototypes. */
void add_protected_object_access(function_access_t *access, object_t *object);
/** @brief Grants descendant protected rights only along explicit inheritance edges. */
void add_inherited_receiver_access(function_access_t *access,
                                   object_t *receiver,
                                   const function_access_t *declaration);
/** @brief Adds one stable capture, validating that storage owns the cell. */
bool add_function_field_capture(function_access_t *access, object_t *storage, field_cell_t *cell);
void mark_function_access(const function_access_t *access);
/** @brief deep=false frees metadata without dereferencing GC-managed objects during sweep. */
void clear_function_access(function_access_t *access, bool deep);
/** @brief Dynamic function metadata, or NULL for built-in/static functions. */
function_access_t *get_function_access(object_t *function);

/** @brief Borrowed lookup result. Absence and denial are distinct statuses. */
typedef struct {
    model_status_t status;
    object_t *storage;
    field_cell_t *cell;
    object_t *value;
} property_lookup_t;

/** @brief Resolves a declaration identity in lexical storage, never through the receiver. */
property_lookup_t lookup_bound_property(const function_access_t *access,
                                        object_t *current,
                                        module_t *unit,
                                        uint32_t declaration_id);

/** @brief Resolves a property without skipping a denied winner.
 * private_slots selects declaration-bound private slots before ordinary lookup.
 */
property_lookup_t lookup_accessible_property(const function_access_t *access,
                                             object_t *object,
                                             object_t *key,
                                             bool private_slots);
bool can_access_field(const function_access_t *access, const field_cell_t *cell);
model_status_t create_accessible_property(const function_access_t *access,
                                          object_t *object,
                                          object_t *key,
                                          object_t *value,
                                          bool constant);
model_status_t write_accessible_property(const function_access_t *access,
                                         object_t *object,
                                         object_t *key,
                                         object_t *value);
/** @brief Language reads return null for absence; failures carry standard exceptions. */
operation_result_t
read_accessible_property(const function_access_t *access, object_t *object, object_t *key);
operation_result_t access_status_result(model_status_t status, object_t *value);
/** @brief Specialized indices retain virtual semantics; dictionary indices use access checks. */
operation_result_t access_index(process_t *process,
                                const function_access_t *access,
                                object_t *object,
                                object_array_t indices,
                                object_t *value);
/** @brief Constructor assignment may introduce own fields after checking the inherited winner. */
bool object_shadows_inherited(const object_t *object);
/** @brief Actual prototype edges, excluding the separate lexical lookup fallback. */
object_array_t get_inheritance_prototypes(const object_t *object);
void set_inheritance_prototype_count(object_t *object, size_t count);
/** @brief Copies using the caller's module; internal transfer does not filter hidden fields. */
object_t *
clone_object_with_access(process_t *process, object_t *object, const function_access_t *access);
/** @brief Transfers actual ancestors once, retaining hidden cells and ordinary lookup winners. */
object_t *
flat_object_with_access(process_t *process, object_t *object, const function_access_t *access);
