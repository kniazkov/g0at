/** @file module_interface.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Metadata-only consumer interpretation of immutable exported contracts.
 */
#include "module_interface.h"

#include "abstract_state.h"
#include "function_summary.h"
#include "graph/declarations.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "module/module_json_internal.h"

#include <string.h>

/** The object domain previously had only its singleton. Derived module scopes
 * preserve that domain; joins widen them to the singleton rather than inventing
 * object identity, properties of arbitrary objects, or closure state. */
typedef struct {
    lattice_element_t base;
    const module_interface_t *interface;
} module_scope_element_t;

const lattice_element_t *make_module_scope_element(arena_t *arena,
                                                   const module_interface_t *interface) {
    module_scope_element_t *scope = alloc_from_arena(arena, sizeof(*scope));
    *scope = (module_scope_element_t){{LATTICE_USER_DEFINED_OBJECT}, interface};
    return &scope->base;
}

static int64_t integer(yyjson_val *value) {
    int64_t result = 0;
    goat_json_integer(value, &result);
    return result;
}

static bool text_is(yyjson_val *value, const char *text) {
    return yyjson_is_str(value) && yyjson_get_len(value) == strlen(text)
           && !memcmp(yyjson_get_str(value), text, strlen(text));
}

const lattice_element_t *module_interface_facts(yyjson_val *facts, arena_t *arena) {
    yyjson_val *type = yyjson_obj_get(facts, "type"), *value = yyjson_obj_get(facts, "value");
    if (text_is(type, "null"))
        return make_null_element();
    if (text_is(type, "boolean"))
        return !value                   ? make_boolean_element()
               : yyjson_get_bool(value) ? make_true_element()
                                        : make_false_element();
    if (text_is(type, "integer")) {
        if (value)
            return make_integer_constant_element(arena, integer(value));
        yyjson_val *range = yyjson_obj_get(facts, "range");
        return range ? make_integer_range_element(arena,
                                                  integer(yyjson_obj_get(range, "min")),
                                                  integer(yyjson_obj_get(range, "max")))
                     : make_integer_element();
    }
    if (text_is(type, "real")) {
        if (!value)
            return make_real_element();
        uint64_t bits = 0;
        const char *hex = yyjson_get_str(value);
        for (size_t i = 0; i < 16; i++)
            bits = (bits << 4) | (uint64_t)(hex[i] <= '9' ? hex[i] - '0' : hex[i] - 'a' + 10);
        /* The existing lattice coalesces NaNs. Do not turn that coarser domain
           back into a promise of a particular payload. */
        if ((bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000)
            && (bits & UINT64_C(0x000fffffffffffff)))
            return make_real_element();
        double real;
        memcpy(&real, &bits, sizeof(real));
        return make_real_constant_element(arena, real);
    }
    if (text_is(type, "numeric"))
        return make_numeric_element();
    if (text_is(type, "string") || text_is(type, "character")) {
        bool character = text_is(type, "character");
        if (!value)
            return character ? make_char_element() : make_string_element();
        string_value_t decoded = decode_utf8_n(yyjson_get_str(value), yyjson_get_len(value));
        const lattice_element_t *result =
            character ? make_char_constant_element(arena, decoded.data[0])
                      : make_string_constant_element(
                            arena,
                            copy_string_to_arena(arena, decoded.data, decoded.length));
        FREE_STRING(decoded);
        return result;
    }
    if (text_is(type, "function"))
        return make_function_element();
    if (text_is(type, "object"))
        return make_user_defined_object_element();
    if (text_is(type, "array")) {
        if (yyjson_is_true(yyjson_obj_get(facts, "empty")))
            return make_typed_array_element(arena, LATTICE_BOTTOM);
        yyjson_val *elements = yyjson_obj_get(facts, "elements");
        if (elements) {
            const lattice_element_t *element = module_interface_facts(elements, arena);
            lattice_type_t kind = element->type;
            if (kind != LATTICE_TRUE && kind != LATTICE_FALSE)
                kind = function_summary_type(element)->type;
            return make_typed_array_element(arena, kind);
        }
        return make_array_element();
    }
    return yyjson_is_true(yyjson_obj_get(facts, "nonNull")) ? make_not_null_element()
                                                            : make_top_element();
}

bool is_module_scope_element(const lattice_element_t *value) {
    return value->type == LATTICE_USER_DEFINED_OBJECT
           && value != make_user_defined_object_element();
}

const lattice_element_t *
module_interface_member(const lattice_element_t *object, string_view_t name, arena_t *arena) {
    if (!is_module_scope_element(object))
        return make_top_element();
    const module_interface_t *interface = ((const module_scope_element_t *)object)->interface;
    size_t length;
    char *utf8 = encode_utf8_view(name, &length);
    yyjson_val *external =
        yyjson_obj_getn(yyjson_obj_get(interface->metadata, "externals"), utf8, length);
    FREE(utf8);
    if (!external || yyjson_get_bool(yyjson_obj_get(external, "mutable")))
        return make_top_element();
    yyjson_val *owner = interface->metadata;
    yyjson_val *id = yyjson_obj_get(external, "functionId");
    yyjson_val *reference = yyjson_obj_get(external, "functionRef");
    if (reference) {
        owner = interface->resolve
                    ? interface->resolve(interface->context,
                                         yyjson_get_str(yyjson_obj_get(reference, "module")),
                                         yyjson_get_str(yyjson_obj_get(reference, "version")))
                    : NULL;
        id = yyjson_obj_get(reference, "functionId");
    }
    if (id && owner) {
        size_t i, max;
        yyjson_val *record;
        yyjson_arr_foreach(yyjson_obj_get(owner, "functions"), i, max, record) {
            if (integer(yyjson_obj_get(record, "functionId")) != integer(id))
                continue;
            imported_function_t *imported = alloc_from_arena(arena, sizeof(*imported));
            *imported = (imported_function_t){yyjson_get_str(yyjson_obj_get(owner, "name")),
                                              yyjson_get_str(yyjson_obj_get(owner, "version")),
                                              (uint32_t)integer(id),
                                              record};
            known_function_element_t *function = alloc_zeroed_from_arena(arena, sizeof(*function));
            function->base.type = LATTICE_KNOWN_FUNCTION;
            function->imported = imported;
            return &function->base;
        }
    }
    return module_interface_facts(external, arena);
}

/** Check implication, not mere overlap: unknown arguments cannot select a narrower summary. */
static bool covers(yyjson_val *facts, const lattice_element_t *argument, arena_t *arena) {
    if (!facts || !yyjson_obj_size(facts))
        return true;
    const lattice_element_t *required = module_interface_facts(facts, arena);
    if (required->type == LATTICE_TOP)
        return true;
    if (required->type == LATTICE_NOT_NULL)
        return argument->type != LATTICE_TOP && argument->type != LATTICE_NULL;
    yyjson_val *elements = yyjson_obj_get(facts, "elements");
    if (elements && yyjson_obj_size(elements)) {
        /* A general/typed array cannot prove refinements erased by its lattice
           representation. Compare the full element constraint recursively. */
        if (argument->type != LATTICE_TYPED_ARRAY)
            return false;
        lattice_element_t element = {((const typed_array_element_t *)argument)->element_type};
        if (element.type != LATTICE_BOTTOM && !covers(elements, &element, arena))
            return false;
    }
    if (yyjson_is_true(yyjson_obj_get(facts, "empty")))
        return argument->type == LATTICE_TYPED_ARRAY
               && ((const typed_array_element_t *)argument)->element_type == LATTICE_BOTTOM;
    if (required->type == LATTICE_INTEGER_RANGE) {
        const integer_range_element_t *range = (const integer_range_element_t *)required;
        if (argument->type == LATTICE_INTEGER_CONSTANT) {
            int64_t n = ((const integer_constant_element_t *)argument)->value;
            return n >= range->min && n <= range->max;
        }
        if (argument->type != LATTICE_INTEGER_RANGE)
            return false;
        const integer_range_element_t *other = (const integer_range_element_t *)argument;
        return other->min >= range->min && other->max <= range->max;
    }
    if (yyjson_obj_get(facts, "value")) {
        if (required->type != argument->type)
            return false;
        switch (required->type) {
            case LATTICE_INTEGER_CONSTANT:
                return ((const integer_constant_element_t *)required)->value
                       == ((const integer_constant_element_t *)argument)->value;
            case LATTICE_REAL_CONSTANT:
                return !memcmp(&((const real_constant_element_t *)required)->value,
                               &((const real_constant_element_t *)argument)->value,
                               sizeof(double));
            case LATTICE_CHAR_CONSTANT:
                return ((const char_constant_element_t *)required)->value
                       == ((const char_constant_element_t *)argument)->value;
            case LATTICE_STRING_CONSTANT: {
                string_view_t a = ((const string_constant_element_t *)required)->value;
                string_view_t b = ((const string_constant_element_t *)argument)->value;
                return a.length == b.length && !MEMCMP(a.data, b.data, a.length);
            }
            case LATTICE_NULL:
            case LATTICE_TRUE:
            case LATTICE_FALSE:
                return true;
            default:
                return false; /* e.g. NaN payloads lost by the lattice */
        }
    }
    lattice_type_t r = function_summary_type(required)->type;
    lattice_type_t a = function_summary_type(argument)->type;
    return r == a || (r == LATTICE_NUMERIC && (a == LATTICE_INTEGER || a == LATTICE_REAL));
}

const lattice_element_t *interpret_imported_call(const imported_function_t *function,
                                                 const lattice_element_t *const *args,
                                                 size_t count,
                                                 abstract_state_t *state,
                                                 uint32_t *effects_out) {
    if (effects_out)
        *effects_out = FUNCTION_EFFECT_UNKNOWN;
    if (state->control_flow != FLOW_NORMAL)
        return make_bottom_element();
    for (size_t i = 0; i < count; i++) {
        if (args[i]->type == LATTICE_BOTTOM) {
            state->control_flow = FLOW_UNREACHABLE;
            return make_bottom_element();
        }
    }
    const lattice_element_t *result = make_top_element();
    uint32_t effects = FUNCTION_EFFECT_UNKNOWN;
    bool complete_effects = false;
    size_t i, max;
    yyjson_val *summary;
    yyjson_val *summaries =
        yyjson_obj_get(yyjson_obj_get(function->record, "analysis"), "summaries");
    yyjson_arr_foreach(summaries, i, max, summary) {
        yyjson_val *conditions = yyjson_obj_get(yyjson_obj_get(summary, "when"), "arguments");
        bool applies = true;
        size_t j, size;
        yyjson_val *condition;
        yyjson_arr_foreach(conditions, j, size, condition) applies &=
            covers(condition, j < count ? args[j] : make_null_element(), state->arena);
        if (!applies)
            continue;
        const lattice_element_t *candidate =
            yyjson_is_false(yyjson_obj_get(summary, "normalReturn"))
                ? make_bottom_element()
                : module_interface_facts(yyjson_obj_get(summary, "result"), state->arena);
        result = lattice_meet(state->arena, result, candidate);
        yyjson_val *effect = yyjson_obj_get(summary, "effects");
        if (effect && yyjson_is_false(yyjson_obj_get(effect, "unknown"))) {
            uint32_t mask = 0;
            yyjson_val *category;
            size_t k, n;
            yyjson_arr_foreach(yyjson_obj_get(effect, "may"), k, n, category) {
                if (text_is(category, "input"))
                    mask |= FUNCTION_EFFECT_INPUT;
                if (text_is(category, "output"))
                    mask |= FUNCTION_EFFECT_OUTPUT;
                if (text_is(category, "externalRead"))
                    mask |= FUNCTION_EFFECT_EXTERNAL_READ;
                if (text_is(category, "externalWrite"))
                    mask |= FUNCTION_EFFECT_EXTERNAL_WRITE;
            }
            effects = complete_effects ? effects & mask : mask;
            complete_effects = true;
        }
    }
    if (effects_out)
        *effects_out = effects;
    if (effects & (FUNCTION_EFFECT_UNKNOWN | FUNCTION_EFFECT_EXTERNAL_WRITE))
        forget_abstract_values(state);
    if (result->type == LATTICE_BOTTOM)
        state->control_flow = FLOW_UNREACHABLE;
    return result;
}

const lattice_element_t *module_interface_member_at(const lattice_element_t *object,
                                                    string_view_t name,
                                                    arena_t *arena,
                                                    const node_t *observer) {
    for (const scope_t *scope = observer ? observer->scope : NULL; scope; scope = scope->parent) {
        const declarator_t *decl = find_symbol_in_scope(scope, name.data);
        if (decl && decl != get_builtin_declarator() && decl->modifier == ACCESS_PRIVATE)
            return make_top_element();
    }
    return module_interface_member(object, name, arena);
}
