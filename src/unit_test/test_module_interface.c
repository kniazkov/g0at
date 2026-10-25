/** @file test_module_interface.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Exact values, conservative summary selection and metadata-only references.
 */
#include "test_module_interface.h"

#include "analysis/abstract_state.h"
#include "analysis/function_call.h"
#include "analysis/function_summary.h"
#include "analysis/module_interface.h"
#include "test_macro.h"

#include <stdio.h>
#include <string.h>

static yyjson_doc *raw(const char *json) {
    return yyjson_read(json, strlen(json), YYJSON_READ_NUMBER_AS_RAW);
}

bool test_module_interface_values() {
    arena_t *arena = create_arena(8);
    const char *texts[] = {"{\"type\":\"integer\",\"value\":9007199254740993}",
                           "{\"type\":\"integer\",\"value\":-9223372036854775808}",
                           "{\"type\":\"integer\",\"value\":9223372036854775807}",
                           "{\"type\":\"integer\",\"range\":{\"min\":-2,\"max\":7}}",
                           "{\"type\":\"string\",\"value\":\"a\\u0000b\"}",
                           "{\"type\":\"character\",\"value\":\"\\ud83d\\ude00\"}",
                           "{\"type\":\"real\",\"value\":\"8000000000000000\"}",
                           "{\"type\":\"real\",\"value\":\"7ff0000000000001\"}",
                           "{\"type\":\"array\",\"empty\":true}",
                           "{\"nonNull\":true}",
                           "{}",
                           "{\"type\":\"boolean\",\"value\":false}"};
    const lattice_element_t *values[12];
    for (size_t i = 0; i < 12; i++) {
        yyjson_doc *doc = raw(texts[i]);
        ASSERT(doc);
        values[i] = module_interface_facts(yyjson_doc_get_root(doc), arena);
        yyjson_doc_free(doc);
    }
    ASSERT(((const integer_constant_element_t *)values[0])->value == INT64_C(9007199254740993));
    ASSERT(((const integer_constant_element_t *)values[1])->value == INT64_MIN);
    ASSERT(((const integer_constant_element_t *)values[2])->value == INT64_MAX);
    ASSERT(values[3]->type == LATTICE_INTEGER_RANGE);
    ASSERT(((const integer_range_element_t *)values[3])->min == -2);
    ASSERT(((const integer_range_element_t *)values[3])->max == 7);
    string_view_t string = ((const string_constant_element_t *)values[4])->value;
    ASSERT(string.length == 3 && string.data[0] == U'a' && !string.data[1]
           && string.data[2] == U'b');
    ASSERT(((const char_constant_element_t *)values[5])->value == 0x1f600);
    uint64_t bits;
    memcpy(&bits, &((const real_constant_element_t *)values[6])->value, sizeof(bits));
    ASSERT(bits == UINT64_C(0x8000000000000000));
    ASSERT(values[7]->type == LATTICE_REAL); /* NaN payload cannot survive lattice joins. */
    ASSERT(values[8]->type == LATTICE_TYPED_ARRAY);
    ASSERT(((const typed_array_element_t *)values[8])->element_type == LATTICE_BOTTOM);
    ASSERT(values[9]->type == LATTICE_NOT_NULL && values[10]->type == LATTICE_TOP);
    ASSERT(values[11]->type == LATTICE_FALSE);
    destroy_arena(arena);
    return true;
}

bool test_module_interface_calls() {
    arena_t *arena = create_arena(8);
    yyjson_doc *doc =
        raw("{\"analysis\":{\"summaries\":["
            "{\"when\":{\"arguments\":[{\"type\":\"integer\",\"range\":{\"min\":1,\"max\":8}}]},"
            "\"result\":{\"type\":\"integer\",\"range\":{\"min\":2,\"max\":10}},\"effects\":{"
            "\"may\":[],\"unknown\":false}},"
            "{\"when\":{\"arguments\":[{\"type\":\"integer\",\"value\":3}]},"
            "\"result\":{\"type\":\"integer\",\"value\":6}},"
            "{\"when\":{\"arguments\":[{\"type\":\"null\"}]},\"normalReturn\":false},"
            "{\"when\":{\"arguments\":[{\"type\":\"real\",\"value\":\"7ff0000000000001\"}]},"
            "\"result\":{\"type\":\"boolean\",\"value\":true}}]}}");
    ASSERT(doc);
    imported_function_t function = {"owner", "1", 1, yyjson_doc_get_root(doc)};
    abstract_state_t *state = create_abstract_state(arena);
    const lattice_element_t *args[] = {make_integer_constant_element(arena, 3), make_top_element()};
    uint32_t effects;
    const lattice_element_t *result = interpret_imported_call(&function, args, 2, state, &effects);
    ASSERT(result->type == LATTICE_INTEGER_CONSTANT);
    ASSERT(((const integer_constant_element_t *)result)->value == 6 && !effects);
    args[0] = make_integer_range_element(arena, 1, 8);
    result = interpret_imported_call(&function, args, 1, state, &effects);
    ASSERT(result->type == LATTICE_INTEGER_RANGE && !effects);
    args[0] = make_integer_range_element(arena, 0, 8);
    result = interpret_imported_call(&function, args, 1, state, &effects);
    ASSERT(result->type == LATTICE_TOP && effects == FUNCTION_EFFECT_UNKNOWN);
    args[0] = make_top_element();
    ASSERT(interpret_imported_call(&function, args, 1, state, NULL)->type == LATTICE_TOP);
    args[0] = make_real_element();
    ASSERT(interpret_imported_call(&function, args, 1, state, NULL)->type == LATTICE_TOP);
    ASSERT(interpret_imported_call(&function, args, 0, state, NULL)->type == LATTICE_BOTTOM);
    ASSERT(state->control_flow == FLOW_UNREACHABLE);
    destroy_abstract_state(state);
    yyjson_doc_free(doc);
    destroy_arena(arena);
    return true;
}

typedef struct {
    yyjson_val *owner;
    size_t calls;
} resolver_t;

static yyjson_val *resolve(void *context, const char *name, const char *version) {
    resolver_t *resolver = context;
    resolver->calls++;
    return !strcmp(name, "owner") && !strcmp(version, "2") ? resolver->owner : NULL;
}

bool test_module_interface_references() {
    arena_t *arena = create_arena(8);
    yyjson_doc *owner = raw("{\"name\":\"owner\",\"version\":\"2\",\"functions\":["
                            "{\"functionId\":4294967295,\"parameters\":[]}]}");
    yyjson_doc *middle =
        raw("{\"name\":\"middle\",\"version\":\"1\",\"externals\":{"
            "\"f\":{\"mutable\":false,\"type\":\"function\",\"functionRef\":{"
            "\"module\":\"owner\",\"version\":\"2\",\"functionId\":4294967295}},"
            "\"changing\":{\"mutable\":true},\"unknown\":{\"mutable\":false},"
            "\"false\":{\"mutable\":false,\"type\":\"boolean\",\"value\":false}}}");
    ASSERT(owner && middle);
    resolver_t resolver = {yyjson_doc_get_root(owner), 0};
    module_interface_t interface = {yyjson_doc_get_root(middle), resolve, &resolver};
    const lattice_element_t *scope = make_module_scope_element(arena, &interface);
    const lattice_element_t *value =
        module_interface_member(scope, (string_view_t){U"f", 1}, arena);
    ASSERT(value->type == LATTICE_KNOWN_FUNCTION && resolver.calls == 1);
    const imported_function_t *function = ((const known_function_element_t *)value)->imported;
    ASSERT(function && function->function_id == UINT32_MAX && !strcmp(function->module, "owner"));
    ASSERT(!((const known_function_element_t *)value)->node);
    ASSERT(module_interface_member(scope, (string_view_t){U"changing", 8}, arena)->type
           == LATTICE_TOP);
    ASSERT(module_interface_member(scope, (string_view_t){U"unknown", 7}, arena)->type
           == LATTICE_TOP);
    ASSERT(module_interface_member(scope, (string_view_t){U"missing", 7}, arena)->type
           == LATTICE_TOP);
    ASSERT(module_interface_member(scope, (string_view_t){U"false", 5}, arena)->type
           == LATTICE_FALSE);
    module_interface_t other = {yyjson_doc_get_root(owner), NULL, NULL};
    const lattice_element_t *joined =
        lattice_join(arena, scope, make_module_scope_element(arena, &other));
    ASSERT(joined == make_user_defined_object_element());
    ASSERT(module_interface_member(joined, (string_view_t){U"f", 1}, arena)->type == LATTICE_TOP);
    yyjson_doc_free(owner);
    yyjson_doc_free(middle);
    destroy_arena(arena);
    return true;
}
