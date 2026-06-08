/**
 * @file visualization.c
 * @copyright 2026 Ivan Kniazkov
 * @brief AST visualization using GraphViz DOT format.
 * 
 * Provides functionality to convert abstract syntax trees to DOT format
 * and generate graph images using GraphViz 'dot' tool.
 */

#include "variable.h"
#include "visualization.h"
#include "lib/allocate.h"
#include "lib/avl_tree.h"
#include "lib/io.h"
#include "lib/string_ext.h"
#include "lib/vector.h"
#include "codegen/source_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool is_graphviz_available() {
#if defined(_WIN32) || defined(_WIN64)
    return system("dot.exe --version > NUL 2>&1") == 0;
#else
    return system("dot --version >/dev/null 2>&1") == 0;
#endif
}

/**
 * @brief Processes input string with trimming, escaping and length limiting
 * 
 * Handles:
 * - Empty input (returns empty string)
 * - Newlines, tabs, quotes (converts to visible escape sequences)
 * - HTML special characters (converts to entities)
 * - Length limit (50 chars + ellipsis if truncated)
 * 
 * @param input Input string to process
 * @return string_value_t Processed string (allocated, needs freeing)
 */
string_value_t trim_and_escape_html_entities(string_value_t input) {
    if (input.length == 0) {
        return EMPTY_STRING_VALUE;
    }

    const size_t MAX_LENGTH = 50;
    string_builder_t builder;
    init_string_builder(&builder, MAX_LENGTH + 4); // +4 for ellipsis and margin

    bool truncated = false;
    size_t remaining = MAX_LENGTH;

    for (size_t index = 0; index < input.length; index++) {
        wchar_t c = input.data[index];
        
        size_t char_len = 1;
        switch (c) {
            case '\n': case '\r': case '\t': 
            case '\"': case '\'': case '\\':
                char_len = 2;
                break;
            case '&': char_len = 5; break; // &amp;
            case '<': case '>': char_len = 4; break; // &lt; &gt;
        }

        if (char_len > remaining) {
            truncated = true;
            break;
        }

        switch (c) {
            case '\n': append_string(&builder, L"\\n"); break;
            case '\r': append_string(&builder, L"\\r"); break;
            case '\t': append_string(&builder, L"\\t"); break;
            case '\"': append_string(&builder, L"\\\""); break;
            case '\'': append_string(&builder, L"\\'"); break;
            case '\\': append_string(&builder, L"\\\\"); break;
            case '&': append_string(&builder, L"&amp;"); break;
            case '<': append_string(&builder, L"&lt;"); break;
            case '>': append_string(&builder, L"&gt;"); break;
            default:
                if (c >= 32 || c == ' ') {
                    append_char(&builder, c);
                } else {
                    char_len = 0;
                }
                break;
        }
        remaining -= char_len;
    }

    if (truncated) {
        append_string(&builder, L"...");
    }

    return (string_value_t){ builder.data, builder.length, true };
}

/**
 * @brief Builds a table with additional node properties.
 *
 * Uses the node virtual-table helpers to retrieve debug/visualization
 * properties exposed by the node. If the node has no properties, returns an
 * empty string value.
 *
 * The resulting string is intended to be embedded directly into a GraphViz HTML
 * label below the node's main data.
 *
 * @param node Node whose properties should be rendered.
 * @return HTML fragment with a two-column property table, or empty string.
 */
static string_value_t build_node_properties_html(const node_t *node) {
    size_t count = get_node_property_count(node);
    if (count == 0) {
        return EMPTY_STRING_VALUE;
    }

    string_value_t result = EMPTY_STRING_VALUE;
    string_builder_t builder = {0};
    for (size_t index = 0; index < count; index++) {
        string_value_t value = EMPTY_STRING_VALUE;
        const wchar_t *key = get_node_property(node, index, &value);
        if (key == NULL) {
            FREE_STRING(value);
            continue;
        }
        string_value_t escaped_value = trim_and_escape_html_entities(value);
        append_string(&builder, L"<br/>");
        append_string(&builder, key);
        append_string(&builder, L": <font color='blue'>");
        append_string_value(&builder, escaped_value);
        result = append_string(&builder, L"</font>");

        FREE_STRING(escaped_value);
        FREE_STRING(value);
    }
    return result;
}

/**
 * @brief Recursively converts an AST node and its child subtree to DOT format.
 *
 * Emits the DOT node definition for the given AST node, recursively emits all
 * child nodes, and writes DOT edges for direct parent-child relationships.
 *
 * During traversal, the function also fills two helper collections used by
 * later visualization passes:
 * - `all_nodes` receives every visited AST node in traversal order.
 * - `nodes_to_ids` maps each AST node pointer to its generated DOT node ID.
 *
 * @param node Node to convert. Must not be NULL.
 * @param last_node_id Pointer to the last assigned DOT node ID. Incremented for
 *        each emitted node.
 * @param all_nodes Vector that receives all visited AST nodes.
 * @param nodes_to_ids AVL tree mapping AST node pointers to DOT node IDs.
 * @param current_scope_id Scope ID of the surrounding scope cluster.
 * @param indent DOT source indentation level.
 * @param builder Source builder accumulating DOT output.
 * @return Generated DOT node ID for `node`.
 */
static int node_to_dot(const node_t* node, uint32_t* last_node_id, vector_t* all_nodes,
        avl_tree_t* nodes_to_ids, unsigned int current_scope_id, 
        size_t indent, source_builder_t* builder) {
    bool new_scope = false;
    if (node->scope->id != current_scope_id) {
        new_scope = true;
        add_formatted_source(
            builder,
            indent,
            format_string(
                L"subgraph cluster_%d { style=\"rounded,dashed\"; color=gray;",
                node->scope->id
            )
        );
        indent++;
    }
    uint32_t id = ++(*last_node_id);
    append_to_vector(all_nodes, (void*)node);
    set_in_avl_tree(nodes_to_ids, (void*)node, (value_t){ .uint32_val = id });        
    const wchar_t* name = node->vtbl->type_name;
    string_value_t value = get_node_data(node);
    string_value_t properties = build_node_properties_html(node);
    const wchar_t *node_color = node->id ? L"black" : L"silver";
    if (value.length > 0) {
        const wchar_t *font_color = L"blue";
        if (node->vtbl->type == NODE_VARIABLE) {
            variable_t *var = (variable_t*)node;
            if (var->declarator->name.data[0] == L'*') {
                // build-in object
                font_color = L"purple";
            }
        }
        string_value_t formatted_value = trim_and_escape_html_entities(value);
        add_formatted_source(
            builder,
            indent,
            format_string(
                L"node_%u [label=<%s<br/><font color='%s'>%s</font>%s> color=%s];",
                id,
                name,
                font_color,
                formatted_value.data,
                properties.data,
                node_color
            )
        );
        FREE_STRING(formatted_value);
    } else if (properties.length > 0) {
        add_formatted_source(
            builder,
            indent,
            format_string(
                L"node_%u [label=<%s%s> color=%s];",
                id,
                name,
                properties.data,
                node_color
            )
        );
    } else {
        add_formatted_source(
            builder,
            indent,
            format_string(
                L"node_%u [label=\"%s\" color=%s];",
                id,
                name,
                node_color
            )
        );
    }
    FREE_STRING(properties);
    FREE_STRING(value);
    size_t count = get_node_child_count(node);
    for (size_t index = 0; index < count; index++) {
        int child_id = node_to_dot(
            get_node_child(node, index),
            last_node_id,
            all_nodes,
            nodes_to_ids,
            node->scope->id,
            indent,
            builder
        );
        const wchar_t* tag = get_node_child_tag(node, index);
        if (tag == NULL) {
            add_formatted_source(
                builder,
                indent,
                format_string(
                    L"node_%u -> node_%u [label=\" %zu\"];",
                    id,
                    child_id,
                    index
                )
            );
        } else {
            add_formatted_source(
                builder,
                indent,
                format_string(
                    L"node_%u -> node_%u [label=\" %s\"];",
                    id,
                    child_id,
                    tag
                )
            );
        }
    }
    if (new_scope) {
        add_static_source(builder, indent - 1, L"}");
    }
    return id;
}

/**
 * @brief Emits DOT edges for non-child relations between AST nodes.
 *
 * Iterates over all AST nodes collected during DOT generation and emits dashed
 * edges for every related node exposed by the node virtual table. These edges
 * represent semantic links rather than parent-child AST structure, such as a
 * variable usage referring to its declarator.
 *
 * The function uses `nodes_to_ids` to translate AST node pointers into DOT node
 * IDs assigned earlier by `node_to_dot()`.
 *
 * @param all_nodes Vector containing all AST nodes visited by `node_to_dot()`.
 * @param nodes_to_ids AVL tree mapping AST node pointers to DOT node IDs.
 * @param indent DOT source indentation level.
 * @param builder Source builder accumulating DOT output.
 */
static void append_related_edges_to_dot(const vector_t *all_nodes, const avl_tree_t *nodes_to_ids,
        size_t indent, source_builder_t *builder) {
    for (size_t node_index = 0; node_index < all_nodes->size; node_index++) {
        const node_t *node = (const node_t*)all_nodes->data[node_index];
        value_t source_id = get_from_avl_tree(nodes_to_ids, (void*)node);
        if (source_id.uint32_val == 0) {
            continue;
        }
        size_t related_count = get_node_related_count(node);
        for (size_t related_index = 0; related_index < related_count; related_index++) {
            const node_t *related_node = get_node_related(node, related_index);
            if (related_node == NULL) {
                continue;
            }
            value_t target_id = get_from_avl_tree(nodes_to_ids, related_node);
            if (target_id.uint32_val == 0) {
                continue;
            }
            relation_type_t relation_type = get_node_relation_type(node, related_index);
            if (relation_type == RELATION_NONE) {
                continue;
            }
            string_value_t relation_name = relation_type_to_string(relation_type);
            add_formatted_source(
                builder,
                indent,
                format_string(
                    L"node_%u -> node_%u [label=\" %s\", style=dashed, color=navy];",
                    source_id.uint32_val,
                    target_id.uint32_val,
                    relation_name.data
                )
            );
        }
    }
}

/**
 * @brief Compares two AST node pointers for AVL tree ordering.
 *
 * Provides a stable ordering over node addresses so AST node pointers can be
 * used as keys in the `nodes_to_ids` map.
 *
 * @param first First node pointer key.
 * @param second Second node pointer key.
 * @return -1 if `first` is lower than `second`, +1 if greater, or 0 if equal.
 */
static int node_comparator(const void *first, const void *second) {
    uintptr_t first_value = (uintptr_t)first;
    uintptr_t second_value = (uintptr_t)second;

    if (first_value < second_value) {
        return -1;
    }
    if (first_value > second_value) {
        return +1;
    }
    return 0;
}

bool generate_image(const node_t* root_node, const char *graph_output_file) {
    bool result = false;

    source_builder_t *builder = create_source_builder();
    add_static_source(builder, 0, L"digraph AST {");
    add_static_source(
        builder,
        1,
        L"node [shape=box, style=\"rounded\", fontname=\"serif\", fontsize=\"11\", penwidth=\"0.7\"];"
    );
    add_static_source(builder, 1,
        L"edge [fontname=\"serif\", fontsize=\"11\", penwidth=\"0.7\"];");
    add_static_source(builder, 1, L"graph [fontname=\"serif\", fontsize=\"11\"];");
    uint32_t last_node_id = 0;
    vector_t *all_nodes = create_vector();
    avl_tree_t *nodes_to_ids = create_avl_tree(node_comparator);
    node_to_dot(root_node, &last_node_id, all_nodes, nodes_to_ids, 0, 1, builder);
    append_related_edges_to_dot(all_nodes, nodes_to_ids, 1, builder);
    destroy_avl_tree(nodes_to_ids);
    destroy_vector(all_nodes);
    add_static_source(builder, 0, L"}");
    string_value_t dot_code = build_source(builder);
    destroy_source_builder(builder);

    size_t file_name_len = strlen(graph_output_file);
    char *dot_file = (char*)ALLOC(file_name_len + 6); // name + ".dot"
    sprintf(dot_file, "%s.dot", graph_output_file);
    bool flag = write_utf8_file(dot_file, dot_code.data);
    if (!flag) {
        goto cleanup;
    }

#if defined(_WIN32) || defined(_WIN64)
    char *dot_exe = "dot.exe";
#else
    char *dot_exe = "dot";
#endif
    const char *file_type = strrchr(graph_output_file, '.');
    if (file_type == NULL) {
       goto cleanup;
    }
    file_type++;
    char* command = ALLOC(file_name_len * 2 + 64);
    sprintf(command, "%s -T%s -o \"%s\" \"%s\"", dot_exe, file_type, graph_output_file, dot_file);
    if (system(command) != 0) {
        goto cleanup;
    }

    result = true;

cleanup:

    FREE(command);
    remove(dot_file);
    FREE(dot_file);
    FREE_STRING(dot_code);
 
    return result;
}
