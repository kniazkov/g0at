/**
 * @file user_defined_object.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions and methods for user-defined objects in the Goat programming language.
 */

#include "access.h"
#include "common_methods.h"
#include "context.h"
#include "lib/allocate.h"
#include "lib/avl_tree.h"
#include "lib/string_ext.h"
#include "lib/vector.h"
#include "object.h"
#include "object_state.h"
#include "process.h"

#include <assert.h>

/**
 * @brief Defines the maximum capacity of the object pool.
 *
 * Sets the maximum number of objects that can be stored in the object pool before it reaches its
 * capacity.
 */
#define POOL_CAPACITY 1024

/** @brief A user-defined object. */
typedef struct {
    /** @brief The base object that provides common functionality. */
    object_t base;

    /** @brief Reference count. */
    int refs;

    /** @brief The state of the object (e.g., unmarked, marked, or zombie). */
    object_state_t state;

    /** @brief A vector storing the prototypes of the object. */
    vector_t *proto;

    /** @brief Explicit derivation shadows inherited properties on assignment. */
    bool shadow_inherited;

    /** @brief A vector storing the topology of the object. */
    vector_t *topology;

    /** @brief A vector storing keys for all properties of the object. */
    vector_t *keys;

    /**
     * @brief AVL tree storing properties, which are key-value pairs where both keys and values are
     * objects.
     */
    avl_tree_t *properties;

    /** @brief Owns every stable cell; properties is only a borrowed lookup index. */
    vector_t *fields;
    module_t *module;
    access_domain_t *domain;
} object_user_defined_t;

typedef field_cell_t property_value_t;

/** @brief Internal paths for callers that have already validated the lookup. */
static field_cell_t *append_field(object_t *obj,
                                  object_t *key,
                                  object_t *value,
                                  bool constant,
                                  access_modifier_t modifier,
                                  module_t *module,
                                  access_domain_t *domain,
                                  bool selected);
static model_status_t write_field(object_t *storage, field_cell_t *cell, object_t *value);
static void collect_sorted_key(void *data, void *key, value_t value);

/** @brief Creates an empty user-defined object. */
static object_user_defined_t *
create_empty_user_defined_object(process_t *process, object_array_t prototypes, module_t *module);

/** @brief Frees all owned cells; sweep must not dereference already swept objects. */
static void clear_fields(object_user_defined_t *uobj, bool deep) {
    for (size_t i = 0; i < uobj->fields->size; i++) {
        field_cell_t *cell = uobj->fields->data[i];
        if (deep) {
            DECREF(cell->key);
            if (cell->object != &uobj->base)
                DECREF(cell->object);
        }
        release_module_identity(cell->module);
        release_module_identity(cell->binding_unit);
        release_access_domain(cell->domain);
        FREE(cell);
    }
    clear_vector(uobj->fields);
    release_module_identity(uobj->module);
    release_access_domain(uobj->domain);
    uobj->module = NULL;
    uobj->domain = NULL;
}

/** @brief Releases or clears a user-defined object with optional deep cleaning. */
static void release_or_clear(object_user_defined_t *uobj, bool deep_cleaning) {
    if (uobj->state == DYING) {
        return;
    }
    if (deep_cleaning) {
        uobj->state = DYING;
        clear_fields(uobj, true);
        for (size_t index = 0; index < uobj->proto->size; index++) {
            DECREF((object_t *)uobj->proto->data[index]);
        }
    } else {
        clear_fields(uobj, false);
    }
    remove_object_from_list(&uobj->base.process->objects, &uobj->base);
    if (uobj->base.process->user_defined_objects.size == POOL_CAPACITY) {
        destroy_vector(uobj->proto);
        destroy_vector(uobj->topology);
        destroy_vector(uobj->keys);
        destroy_vector(uobj->fields);
        destroy_avl_tree(uobj->properties);
        FREE(uobj);
    } else {
        clear_vector(uobj->proto);
        clear_vector(uobj->topology);
        clear_vector(uobj->keys);
        clear_avl_tree(uobj->properties);
        uobj->refs = 0;
        uobj->state = ZOMBIE;
        add_object_to_list(&uobj->base.process->user_defined_objects, &uobj->base);
    }
}

/** @brief Implements @ref object_vtbl_t::inc_ref. */
static void inc_ref(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    assert(uobj->state != ZOMBIE);
    uobj->refs++;
}

/** @brief Implements @ref object_vtbl_t::dec_ref. */
static void dec_ref(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    assert(uobj->state != ZOMBIE);
    if (!(--uobj->refs)) {
        release_or_clear(uobj, true);
    }
}

/** @brief Implements @ref object_vtbl_t::mark. */
static void mark(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    assert(uobj->state != ZOMBIE);
    if (uobj->state == UNMARKED) {
        uobj->state = MARKED;
        for (size_t i = 0; i < uobj->fields->size; i++) {
            field_cell_t *cell = uobj->fields->data[i];
            mark_object(cell->key);
            mark_object(cell->object);
        }
        for (size_t index = 0; index < uobj->proto->size; index++) {
            mark_object((object_t *)uobj->proto->data[index]);
        }
    }
}

/** @brief Implements @ref object_vtbl_t::sweep. */
static bool sweep(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    assert(uobj->state != ZOMBIE);
    if (uobj->state == UNMARKED) {
        release_or_clear(uobj, false);
        return true;
    } else {
        uobj->state = UNMARKED;
        return false;
    }
}

/** @brief Implements @ref object_vtbl_t::release. */
static void release(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    remove_object_from_list(uobj->state == ZOMBIE ? &obj->process->user_defined_objects
                                                  : &obj->process->objects,
                            obj);
    destroy_vector(uobj->proto);
    destroy_vector(uobj->topology);
    destroy_vector(uobj->keys);
    clear_fields(uobj, false);
    destroy_vector(uobj->fields);
    destroy_avl_tree(uobj->properties);
    FREE(obj);
}

/** @brief Copies all cells, preserving the selected-key index and existing shallow semantics. */
static object_t *
clone_in_module(process_t *process, object_t *obj, module_t *module, bool relocate) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    object_user_defined_t *copy = create_empty_user_defined_object(
        process,
        (object_array_t){(object_t *const *)uobj->proto->data, uobj->proto->size},
        module);
    copy->shadow_inherited = uobj->shadow_inherited;
    vector_t *old_domains = create_vector(), *new_domains = create_vector();
    for (size_t i = 0; i < uobj->fields->size; i++) {
        field_cell_t *cell = uobj->fields->data[i];
        access_domain_t *domain = cell->domain;
        if (relocate) {
            size_t d = 0;
            while (d < old_domains->size && old_domains->data[d] != domain)
                d++;
            if (d == old_domains->size) {
                append_to_vector(old_domains, domain);
                append_to_vector(new_domains,
                                 domain == uobj->domain
                                     ? retain_access_domain(copy->domain)
                                     : create_access_domain(get_access_domain_module(domain)));
            }
            domain = new_domains->data[d];
        }
        field_cell_t *copied = append_field(&copy->base,
                                            cell->key,
                                            cell->object == obj ? &copy->base : cell->object,
                                            cell->is_constant,
                                            cell->modifier,
                                            cell->module,
                                            domain,
                                            get_own_field_cell(obj, cell->key) == cell);
        copied->declaration_id = cell->declaration_id;
        copied->binding_unit =
            cell->binding_unit ? retain_module_identity(cell->binding_unit) : NULL;
    }
    for (size_t i = 0; i < new_domains->size; i++)
        release_access_domain(new_domains->data[i]);
    destroy_vector(old_domains);
    destroy_vector(new_domains);
    /* Preserve clone's historical key order independently of the full cell inventory. */
    clear_vector(copy->keys);
    avl_tree_for_each(copy->properties, collect_sorted_key, copy->keys);
    return &copy->base;
}

static object_t *clone(process_t *process, object_t *obj) {
    return clone_in_module(process, obj, process->module, false);
}

object_t *
clone_object_with_access(process_t *process, object_t *obj, const function_access_t *access) {
    return obj->vtbl->type == TYPE_USER_DEFINED_OBJECT
               ? clone_in_module(process, obj, access->module, true)
               : clone_object(process, obj);
}

/** @brief Context used while rendering an object in Goat notation. */
typedef struct {
    /** @brief The string builder receiving the output. */
    string_builder_t *builder;

    /**
     * @brief The path of user-defined objects currently being rendered.
     *
     * A user-defined object found again on this path is a reference cycle (e.g. the `this`
     * property), so it is rendered as `{...}` instead of recursing forever.
     */
    vector_t *active;

    /** @brief Builder length at the start of the currently rendered object. */
    size_t mark;
} notation_renderer_t;

/** @brief Renders a user-defined object in Goat notation, detecting reference cycles. */
static string_value_t render_user_defined_object_notation(notation_renderer_t *renderer,
                                                          object_user_defined_t *uobj);

/** @brief Renders an array using the same path as dictionary rendering. */
static string_value_t render_array_notation(notation_renderer_t *renderer, const object_t *obj);

/** @brief Appends a value's Goat notation, recursing into containers. */
static void render_object_value_notation(notation_renderer_t *renderer, object_t *value) {
    if (value->vtbl->type == TYPE_USER_DEFINED_OBJECT) {
        render_user_defined_object_notation(renderer, (object_user_defined_t *)value);
    } else if (value->vtbl->type == TYPE_ARRAY) {
        render_array_notation(renderer, value);
    } else {
        string_value_t str = convert_object_to_string_notation(value);
        append_string_value(renderer->builder, str);
        FREE_STRING(str);
    }
}

/**
 * @brief Converts a key-value pair into a Goat notation string and appends it to the string
 * builder.
 *
 * If the builder already contains data, a comma is inserted before appending the new pair.
 */
static void child_pair_to_string(void *data, void *key, value_t value) {
    notation_renderer_t *renderer = (notation_renderer_t *)data;
    if (renderer->builder->length > renderer->mark + 1) {
        append_char(renderer->builder, ',');
    }
    object_t *key_obj = (object_t *)key;
    property_value_t *ref = (property_value_t *)value.ptr;
    render_object_value_notation(renderer, key_obj);
    append_char(renderer->builder, ':');
    render_object_value_notation(renderer, ref->object);
}

/** @brief Renders a user-defined object in Goat notation, detecting reference cycles. */
static string_value_t render_user_defined_object_notation(notation_renderer_t *renderer,
                                                          object_user_defined_t *uobj) {
    for (size_t index = 0; index < renderer->active->size; index++) {
        if (renderer->active->data[index] == &uobj->base) {
            return append_string(renderer->builder, L"{...}");
        }
    }
    if (uobj->properties->root == NULL) {
        return append_string(renderer->builder, L"{ }");
    }
    size_t saved_mark = renderer->mark;
    renderer->mark = renderer->builder->length;
    append_to_vector(renderer->active, &uobj->base);
    append_char(renderer->builder, '{');
    avl_tree_for_each(uobj->properties, child_pair_to_string, renderer);
    string_value_t result = append_char(renderer->builder, '}');
    renderer->active->size--;
    renderer->mark = saved_mark;
    return result;
}

/** @brief Renders a sequence, abbreviating cycles with [...]. */
static string_value_t render_array_notation(notation_renderer_t *renderer, const object_t *obj) {
    for (size_t i = 0; i < renderer->active->size; i++)
        if (renderer->active->data[i] == obj)
            return append_string(renderer->builder, L"[...]");
    append_to_vector(renderer->active, (void *)obj);
    append_char(renderer->builder, L'[');
    object_array_t elements = get_array_elements(obj);
    if (!elements.size)
        append_char(renderer->builder, L' ');
    for (size_t i = 0; i < elements.size; i++) {
        if (i)
            append_char(renderer->builder, L',');
        render_object_value_notation(renderer, elements.items[i]);
    }
    renderer->active->size--;
    return append_char(renderer->builder, L']');
}

string_value_t container_to_string_notation(const object_t *obj) {
    string_builder_t builder;
    init_string_builder(&builder, 2);
    notation_renderer_t renderer = {&builder, create_vector(), 0};
    render_object_value_notation(&renderer, (object_t *)obj);
    destroy_vector(renderer.active);
    return (string_value_t){builder.data, builder.length, true};
}

/** @brief Implements @ref object_vtbl_t::to_string_notation. */
static string_value_t to_string_notation(const object_t *obj) {
    return container_to_string_notation(obj);
}

/** @brief Implements @ref object_vtbl_t::to_string. */
static string_value_t to_string(const object_t *obj) {
    return to_string_notation(obj);
}

/** @brief Implements @ref object_vtbl_t::get_prototypes. */
static object_array_t get_prototypes(const object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    return (object_array_t){(object_t **)uobj->proto->data, uobj->proto->size};
}

/** @brief Implements @ref object_vtbl_t::get_topology. */
static object_array_t get_topology(const object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    return (object_array_t){(object_t **)uobj->topology->data, uobj->topology->size};
}

/** @brief Implements @ref object_vtbl_t::get_keys. */
static object_array_t get_keys(const object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    return (object_array_t){(object_t *const *)uobj->keys->data, uobj->keys->size};
}

/** @brief Implements @ref object_vtbl_t::get_property. */
static object_t *get_property(const object_t *obj, const object_t *key) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    property_value_t *ref = (property_value_t *)(get_from_avl_tree(uobj->properties, key).ptr);
    if (ref)
        return ref->object;
    /* The implicit constant is not stored, so empty objects stay empty and no cycle is added. */
    return key->vtbl->type == TYPE_STRING && compare_objects_using_vtbl(key, get_this_key()) == 0
               ? (object_t *)obj
               : NULL;
}

/** @brief Implements @ref object_vtbl_t::create_property. */
static model_status_t
create_property(object_t *obj, object_t *key, object_t *value, bool constant) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    property_value_t *ref = (property_value_t *)(get_from_avl_tree(uobj->properties, key).ptr);
    if (key->vtbl->type == TYPE_STRING && compare_objects_using_vtbl(key, get_this_key()) == 0
        && (value != obj || !constant))
        return MSTAT_PROPERTY_IS_CONSTANT;
    if (ref
        || (uobj->fields->size != uobj->keys->size
            && get_domain_field_cell(obj, key, uobj->domain))) {
        return MSTAT_PROPERTY_ALREADY_EXISTS;
    }
    append_field(obj, key, value, constant, ACCESS_MODULE, uobj->module, uobj->domain, true);
    return MSTAT_OK;
}

/** @brief Implements @ref object_vtbl_t::set_property. */
static model_status_t set_property(object_t *obj, object_t *key, object_t *value) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    property_value_t *ref = (property_value_t *)(get_from_avl_tree(uobj->properties, key).ptr);
    if (key->vtbl->type == TYPE_STRING && compare_objects_using_vtbl(key, get_this_key()) == 0)
        return MSTAT_PROPERTY_IS_CONSTANT;
    if (!ref) {
        if (uobj->shadow_inherited)
            return create_property(obj, key, value, false);
        return MSTAT_PROPERTY_NOT_FOUND;
    }
    return write_field(obj, ref, value);
}

/** @brief Implements @ref object_vtbl_t::add. */
static operation_result_t add(process_t *process, object_t *obj1, object_t *obj2) {
    return operation_exception(get_exception_invalid_operation());
}

/** @brief Implements @ref object_vtbl_t::subtract. */
static operation_result_t subtract(process_t *process, object_t *obj1, object_t *obj2) {
    return operation_exception(get_exception_invalid_operation());
}

/** @brief Implements @ref object_vtbl_t::multiply. */
static operation_result_t multiply(process_t *process, object_t *obj1, object_t *obj2) {
    return operation_exception(get_exception_invalid_operation());
}

/** @brief Implements @ref object_vtbl_t::divide. */
static operation_result_t divide(process_t *process, object_t *obj1, object_t *obj2) {
    return operation_exception(get_exception_invalid_operation());
}

/** @brief Implements @ref object_vtbl_t::modulo. */
static operation_result_t modulo(process_t *process, object_t *obj1, object_t *obj2) {
    return operation_exception(get_exception_invalid_operation());
}

/** @brief Implements @ref object_vtbl_t::power. */
static operation_result_t power(process_t *process, object_t *obj1, object_t *obj2) {
    return operation_exception(get_exception_invalid_operation());
}

/** @brief Implements @ref object_vtbl_t::get_boolean_value. */

static bool get_boolean_value(const object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    return uobj->properties->root != NULL;
}

/** @brief Virtual table defining the behavior of the user-defined object. */
static object_vtbl_t vtbl = {.type = TYPE_USER_DEFINED_OBJECT,
                             .inc_ref = inc_ref,
                             .dec_ref = dec_ref,
                             .mark = mark,
                             .sweep = sweep,
                             .release = release,
                             .compare = compare_object_addresses,
                             .clone = clone,
                             .to_string = to_string,
                             .to_string_notation = to_string_notation,
                             .get_prototypes = get_prototypes,
                             .get_topology = get_topology,
                             .get_keys = get_keys,
                             .get_property = get_property,
                             .create_property = create_property,
                             .set_property = set_property,
                             .get_index = common_get_index,
                             .set_index = common_set_index,
                             .bitwise_not = stub_unary_operation,
                             .bitwise_and = stub_bitwise,
                             .bitwise_or = stub_bitwise,
                             .bitwise_xor = stub_bitwise,
                             .shift_left = stub_bitwise,
                             .shift_right = stub_bitwise,
                             .unary_plus = stub_unary_operation,
                             .unary_minus = stub_unary_operation,
                             .increment = stub_unary_operation,
                             .decrement = stub_unary_operation,
                             .add = add,
                             .subtract = subtract,
                             .multiply = multiply,
                             .divide = divide,
                             .modulo = modulo,
                             .power = power,
                             .less = common_less,
                             .less_or_equal = common_less_or_equal,
                             .greater = common_greater,
                             .greater_or_equal = common_greater_or_equal,
                             .equal = common_equal,
                             .not_equal = common_not_equal,
                             .get_boolean_value = get_boolean_value,
                             .get_integer_value = stub_get_integer_value,
                             .get_real_value = stub_get_real_value,
                             .call = stub_call};

/** @brief Compares two keys for use in the user-defined object's AVL tree. */
static int key_comparator(const void *first, const void *second) {
    object_t *obj1 = (object_t *)first;
    object_t *obj2 = (object_t *)second;
    if (obj1->vtbl->type > obj2->vtbl->type) {
        return 1;
    } else if (obj1->vtbl->type < obj2->vtbl->type) {
        return -1;
    } else {
        return compare_objects_using_vtbl(obj1, obj2);
    }
}

/** @brief Recursively performs a topological sorting of an object's prototype chain. */
static void topological_sorting(object_t *obj, avl_tree_t *processed, vector_t *topology) {
    if (avl_tree_contains(processed, obj)) {
        return;
    }
    object_array_t proto = get_object_prototypes(obj);
    if (proto.size > 0) {
        size_t index = proto.size;
        do {
            index--;
            topological_sorting(proto.items[index], processed, topology);
        } while (index > 0);
    }
    append_to_vector(topology, obj);
    set_in_avl_tree(processed, obj, (value_t){.ptr = obj});
}

/** @brief Builds the topological order of an object's prototype chain. */
static void build_topology(object_array_t proto, vector_t *topology) {
    assert(topology->size == 0);
    size_t index;
    if (proto.size > 1) {
        // multiple inheritance
        avl_tree_t *processed =
            create_avl_tree((int (*)(const void *, const void *))compare_object_addresses);
        index = proto.size;
        do {
            index--;
            topological_sorting(proto.items[index], processed, topology);
        } while (index > 0);
        destroy_avl_tree(processed);
        reverse_vector(topology);
    } else {
        // single prototype
        object_t *single = proto.items[0];
        append_to_vector(topology, single);
        object_array_t parents = get_object_topology(single);
        for (index = 0; index < parents.size; index++) {
            append_to_vector(topology, parents.items[index]);
        }
    }
}

static object_user_defined_t *
create_empty_user_defined_object(process_t *process, object_array_t proto, module_t *module) {
    assert(proto.size > 0);
    object_user_defined_t *uobj;
    if (process->user_defined_objects.size > 0) {
        uobj =
            (object_user_defined_t *)remove_first_object_from_list(&process->user_defined_objects);
    } else {
        uobj = (object_user_defined_t *)CALLOC(sizeof(object_user_defined_t));
        uobj->base.vtbl = &vtbl;
        uobj->base.process = process;
        uobj->proto = create_vector_ex(proto.size);
        uobj->topology = create_vector();
        uobj->keys = create_vector();
        uobj->fields = create_vector();
        uobj->properties = create_avl_tree(key_comparator);
    }
    uobj->module = retain_module_identity(module);
    uobj->domain = create_access_domain(module);
    uobj->shadow_inherited = false;
    uobj->refs = 1;
    uobj->state = UNMARKED;
    for (size_t index = 0; index < proto.size; index++) {
        INCREF(proto.items[index]);
        append_to_vector(uobj->proto, proto.items[index]);
    }
    build_topology(proto, uobj->topology);
    add_object_to_list(&process->objects, &uobj->base);
    return uobj;
}

object_t *create_user_defined_object(process_t *process, object_array_t prototypes) {
    return create_user_defined_object_in_module(process, prototypes, process->module);
}

void enable_prototype_shadowing(object_t *obj) {
    ((object_user_defined_t *)obj)->shadow_inherited = true;
}

bool object_shadows_inherited(const object_t *obj) {
    return obj->vtbl->type == TYPE_USER_DEFINED_OBJECT
           && ((object_user_defined_t *)obj)->shadow_inherited;
}

object_t *create_user_defined_object_in_module(process_t *process,
                                               object_array_t prototypes,
                                               module_t *module) {
    return &create_empty_user_defined_object(process, prototypes, module)->base;
}

module_t *get_object_module(const object_t *obj) {
    if (obj->vtbl->type == TYPE_USER_DEFINED_OBJECT)
        return ((object_user_defined_t *)obj)->module;
    function_access_t *access = get_function_access((object_t *)obj);
    return access ? access->module : get_builtin_module_identity();
}

access_domain_t *get_object_access_domain(const object_t *obj) {
    return obj->vtbl->type == TYPE_USER_DEFINED_OBJECT ? ((object_user_defined_t *)obj)->domain
                                                       : NULL;
}

size_t get_object_field_count(const object_t *obj) {
    return obj->vtbl->type == TYPE_USER_DEFINED_OBJECT
               ? ((object_user_defined_t *)obj)->fields->size
               : 0;
}

field_cell_t *get_object_field_at(const object_t *obj, size_t index) {
    return index < get_object_field_count(obj) ? ((object_user_defined_t *)obj)->fields->data[index]
                                               : NULL;
}

field_cell_t *get_own_field_cell(const object_t *obj, const object_t *key) {
    if (obj->vtbl->type != TYPE_USER_DEFINED_OBJECT)
        return NULL;
    return get_from_avl_tree(((object_user_defined_t *)obj)->properties, key).ptr;
}

field_cell_t *
get_domain_field_cell(const object_t *obj, const object_t *key, const access_domain_t *domain) {
    for (size_t i = 0; i < get_object_field_count(obj); i++) {
        field_cell_t *cell = get_object_field_at(obj, i);
        if (cell->domain == domain && !key_comparator(cell->key, key))
            return cell;
    }
    return NULL;
}

field_cell_t *create_object_field(object_t *obj,
                                  object_t *key,
                                  object_t *value,
                                  bool constant,
                                  access_modifier_t modifier,
                                  module_t *module,
                                  access_domain_t *domain,
                                  bool selected) {
    assert(obj->vtbl->type == TYPE_USER_DEFINED_OBJECT);
    assert(module && domain);
    assert(modifier >= ACCESS_MODULE && modifier <= ACCESS_PRIVATE);
    if (get_domain_field_cell(obj, key, domain) || (selected && get_own_field_cell(obj, key)))
        return NULL;
    if (key->vtbl->type == TYPE_STRING && !compare_objects_using_vtbl(key, get_this_key())
        && (value != obj || !constant))
        return NULL;
    return append_field(obj, key, value, constant, modifier, module, domain, selected);
}

static field_cell_t *append_field(object_t *obj,
                                  object_t *key,
                                  object_t *value,
                                  bool constant,
                                  access_modifier_t modifier,
                                  module_t *module,
                                  access_domain_t *domain,
                                  bool selected) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    field_cell_t *cell = ALLOC(sizeof(*cell));
    *cell = (field_cell_t){key,
                           value,
                           constant,
                           modifier,
                           retain_module_identity(module),
                           retain_access_domain(domain)};
    INCREF(key);
    if (value != obj)
        INCREF(value);
    append_to_vector(uobj->fields, cell);
    if (selected) {
        append_to_vector(uobj->keys, key);
        set_in_avl_tree(uobj->properties, key, (value_t){.ptr = cell});
    }
    return cell;
}

model_status_t set_field_cell_value(object_t *storage, field_cell_t *cell, object_t *value) {
    bool found = false;
    for (size_t i = 0; i < get_object_field_count(storage); i++)
        if (get_object_field_at(storage, i) == cell) {
            found = true;
            break;
        }
    if (!found)
        return MSTAT_PROPERTY_NOT_FOUND;
    return write_field(storage, cell, value);
}

static model_status_t write_field(object_t *storage, field_cell_t *cell, object_t *value) {
    if (cell->is_constant)
        return MSTAT_PROPERTY_IS_CONSTANT;
    /* Retain first: assigning a borrowed value to its own cell must be safe. */
    if (value != storage)
        INCREF(value);
    if (cell->object != storage)
        DECREF(cell->object);
    cell->object = value;
    return MSTAT_OK;
}

access_modifier_t get_object_property_modifier(const object_t *obj, const object_t *key) {
    field_cell_t *cell = get_own_field_cell(obj, key);
    return cell                                          ? cell->modifier
           : obj->vtbl->type == TYPE_USER_DEFINED_OBJECT ? ACCESS_MODULE
                                                         : ACCESS_PUBLIC;
}

/** @brief Collects keys in dictionary comparison order. */
static void collect_sorted_key(void *data, void *key, value_t value) {
    append_to_vector((vector_t *)data, key);
}

object_t *snapshot_object_keys(process_t *process, object_t *obj) {
    vector_t *keys = create_vector();
    avl_tree_for_each(((object_user_defined_t *)obj)->properties, collect_sorted_key, keys);
    object_t *result =
        create_array_object(process, (object_array_t){(object_t **)keys->data, keys->size});
    destroy_vector(keys);
    return result;
}
