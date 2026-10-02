/**
 * @file user_defined_object.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Definitions and methods for user-defined objects in the Goat programming language.
 */

#include "common_methods.h"
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

    /** @brief A vector storing the topology of the object. */
    vector_t *topology;

    /** @brief A vector storing keys for all properties of the object. */
    vector_t *keys;

    /**
     * @brief AVL tree storing properties, which are key-value pairs where both keys and values are
     * objects.
     */
    avl_tree_t *properties;
} object_user_defined_t;

/** @brief The value of the object property, namely some other object and flags. */
typedef struct {
    /** @brief Object which is the value of the property. */
    object_t *object;

    /** @brief Flag indicating that the property value is constant (immutable). */
    bool is_constant;
} property_value_t;

/** @brief Creates an empty user-defined object. */
static object_user_defined_t *create_empty_user_defined_object(process_t *process,
                                                               object_array_t prototypes);

/**
 * @brief Decrements the reference count of a key-value pair in the user-defined object's children.
 *
 * It decreases the reference count of both the key and the value, ensuring proper memory management
 * and cleanup of referenced objects.
 */
static void clear_child_pair(void *unused, void *key, value_t value) {
    DECREF(key);
    property_value_t *ref = (property_value_t *)value.ptr;
    DECREF(ref->object);
    FREE(ref);
}

/** @brief Removes a reference to a property value without reference counting. */
static void remove_reference(void *unused, void *key, value_t value) {
    FREE(value.ptr);
}

/** @brief Releases or clears a user-defined object with optional deep cleaning. */
static void release_or_clear(object_user_defined_t *uobj, bool deep_cleaning) {
    if (uobj->state == DYING) {
        return;
    }
    if (deep_cleaning) {
        uobj->state = DYING;
        avl_tree_for_each(uobj->properties, clear_child_pair, NULL);
        for (size_t index = 0; index < uobj->proto->size; index++) {
            DECREF((object_t *)uobj->proto->data[index]);
        }
    } else {
        avl_tree_for_each(uobj->properties, remove_reference, NULL);
    }
    remove_object_from_list(&uobj->base.process->objects, &uobj->base);
    if (uobj->base.process->user_defined_objects.size == POOL_CAPACITY) {
        destroy_vector(uobj->proto);
        destroy_vector(uobj->topology);
        destroy_vector(uobj->keys);
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

/** @brief Marks the key-value pair in the user-defined object's children for garbage collection. */
static void mark_child_pair(void *unused, void *key, value_t value) {
    object_t *key_obj = (object_t *)key;
    property_value_t *ref = (property_value_t *)value.ptr;
    mark_object(key_obj);
    mark_object(ref->object);
}

/** @brief Implements @ref object_vtbl_t::mark. */
static void mark(object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    assert(uobj->state != ZOMBIE);
    if (uobj->state == UNMARKED) {
        uobj->state = MARKED;
        avl_tree_for_each(uobj->properties, mark_child_pair, NULL);
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

/** @brief Clearing the memory occupied by properties. */
static void clear_properties(void *unused, void *key, value_t value) {
    property_value_t *ref = (property_value_t *)value.ptr;
    FREE(ref);
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
    avl_tree_for_each(uobj->properties, clear_properties, NULL);
    destroy_avl_tree(uobj->properties);
    FREE(obj);
}

/**
 * @brief Copies a key-value pair from one AVL tree to another during object cloning.
 *
 * Increments the reference count of both the key and the value. 2.
 */
static void copy_child_pair(void *data, void *key, value_t value) {
    object_user_defined_t *copy = (object_user_defined_t *)data;
    property_value_t *ref = (property_value_t *)value.ptr;
    INCREF(key);
    INCREF(ref->object);
    append_to_vector(copy->keys, key);
    property_value_t *copy_ref = (property_value_t *)ALLOC(sizeof(property_value_t));
    memcpy(copy_ref, ref, sizeof(property_value_t));
    set_in_avl_tree(copy->properties, key, (value_t){.ptr = copy_ref});
}

/** @brief Implements @ref object_vtbl_t::clone. */
static object_t *clone(process_t *process, object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    object_user_defined_t *copy = create_empty_user_defined_object(
        process,
        (object_array_t){(object_t *const *)uobj->proto->data, uobj->proto->size});
    avl_tree_for_each(uobj->properties, copy_child_pair, copy);
    return &copy->base;
}

/**
 * @brief Converts a key-value pair into a Goat notation string and appends it to the string
 * builder.
 *
 * If the builder already contains data, a semicolon is inserted before appending the new pair.
 */
static void child_pair_to_string(void *data, void *key, value_t value) {
    string_builder_t *builder = (string_builder_t *)data;
    if (builder->length > 1) {
        append_char(builder, ',');
    }
    object_t *key_obj = (object_t *)key;
    property_value_t *ref = (property_value_t *)value.ptr;
    string_value_t key_str = convert_object_to_string_notation(key_obj);
    append_string_value(builder, key_str);
    FREE_STRING(key_str);
    append_char(builder, ':');
    string_value_t value_str = convert_object_to_string_notation(ref->object);
    append_string_value(builder, value_str);
    FREE_STRING(value_str);
}

/** @brief Implements @ref object_vtbl_t::to_string_notation. */
static string_value_t to_string_notation(const object_t *obj) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    if (uobj->properties->root == NULL) {
        return STATIC_STRING(L"{ }");
    }
    string_builder_t builder;
    init_string_builder(&builder, 2);
    append_char(&builder, '{');
    avl_tree_for_each(uobj->properties, child_pair_to_string, &builder);
    return append_char(&builder, '}');
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
    return ref ? ref->object : NULL;
}

/** @brief Implements @ref object_vtbl_t::create_property. */
static model_status_t
create_property(object_t *obj, object_t *key, object_t *value, bool constant) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    property_value_t *ref = (property_value_t *)(get_from_avl_tree(uobj->properties, key).ptr);
    if (ref) {
        return MSTAT_PROPERTY_ALREADY_EXISTS;
    }
    INCREF(key);
    INCREF(value);
    append_to_vector(uobj->keys, key);
    ref = (property_value_t *)ALLOC(sizeof(property_value_t));
    ref->object = value;
    ref->is_constant = constant;
    set_in_avl_tree(uobj->properties, key, (value_t){.ptr = ref});
    return MSTAT_OK;
}

/** @brief Implements @ref object_vtbl_t::set_property. */
static model_status_t set_property(object_t *obj, object_t *key, object_t *value) {
    object_user_defined_t *uobj = (object_user_defined_t *)obj;
    property_value_t *ref = (property_value_t *)(get_from_avl_tree(uobj->properties, key).ptr);
    if (!ref) {
        return MSTAT_PROPERTY_NOT_FOUND;
    }
    if (ref->is_constant) {
        return MSTAT_PROPERTY_IS_CONSTANT;
    }
    DECREF(ref->object);
    ref->object = value;
    INCREF(value);
    return MSTAT_OK;
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

static object_user_defined_t *create_empty_user_defined_object(process_t *process,
                                                               object_array_t proto) {
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
        uobj->properties = create_avl_tree(key_comparator);
    }
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
    return &create_empty_user_defined_object(process, prototypes)->base;
}
