/** @file module_import.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Lazy module import, without any runtime path resolution.
 */
#include "module_import.h"

#include "codegen/code_builder.h"
#include "codegen/data_builder.h"
#include "codegen/source_builder.h"
#include "common_methods.h"
#include "expression.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"

typedef struct {
    expression_t base;
    string_view_t identity;
    node_t *literal;
} import_t;

static string_value_t generate_goat_code(const node_t *node) {
    string_value_t literal =
        ((const import_t *)node)
            ->literal->vtbl->generate_goat_code(((const import_t *)node)->literal);
    string_value_t result = format_string(U"import %s", literal.data);
    FREE_STRING(literal);
    return result;
}

static void
generate_indented_goat_code(const node_t *node, source_builder_t *builder, size_t indent) {
    append_formatted_source(builder, generate_goat_code(node));
}

static instr_index_t generate_bytecode(node_t *node, code_builder_t *code, data_builder_t *data) {
    uint32_t descriptor = add_string_to_data_segment_ex(data, ((import_t *)node)->identity);
    return add_instruction(code, (instruction_t){.opcode = MODULE_IMPORT, .arg1 = descriptor});
}

static node_vtbl_t import_vtbl = {
    .type = NODE_MODULE_IMPORT,
    .analyze_reachability = reachability_unknown,
    .is_pure = not_pure,
    .simplify = no_simplification,
    .collect_direct_effects = unknown_direct_effects,
    .type_name = U"import",
    .get_data = no_data,
    .get_property_count = no_properties,
    .get_property = no_property,
    .get_child_count = no_children,
    .get_child = no_child,
    .get_child_tag = no_tags,
    .insert_child_before = no_child_insertion,
    .replace_child = no_child_replacement,
    .get_related_count = no_related_nodes,
    .get_related = no_related_node,
    .get_relation_type = no_relation_type,
    .calculate = unknown_abstract_value,
    .execute = execute_nothing,
    .generate_goat_code = generate_goat_code,
    .generate_indented_goat_code = generate_indented_goat_code,
    .generate_bytecode = generate_bytecode,
    .can_generate_c_code = cannot_generate_c_code,
    .generate_c_code = no_c_code,
    .generate_indented_c_code = no_indented_c_code,
    .generate_bytecode_assign = no_bytecode_assignment,
    .generate_bytecode_deferred = no_deferred_bytecode,
};

node_t *create_module_import_node(arena_t *arena, string_view_t identity, node_t *literal) {
    import_t *expr = alloc_zeroed_from_arena(arena, sizeof(*expr));
    expr->base.base.vtbl = &import_vtbl;
    expr->identity = copy_string_to_arena(arena, identity.data, identity.length);
    expr->literal = literal;
    return &expr->base.base;
}
