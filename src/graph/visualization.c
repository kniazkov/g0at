/**
 * @file visualization.c
 * @copyright 2025 Ivan Kniazkov
 * @brief AST visualization using GraphViz DOT format.
 * 
 * Provides functionality to convert abstract syntax trees to DOT format
 * and generate graph images using GraphViz 'dot' tool.
 */

#include "visualization.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "codegen/source_builder.h"

#include <stdlib.h>

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
        return (string_value_t){ L"", 0, false };
    }

    const size_t MAX_LENGTH = 50;
    string_builder_t builder;
    init_string_builder(&builder, MAX_LENGTH + 4); // +4 for ellipsis and margin

    bool truncated = false;
    size_t remaining = MAX_LENGTH;

    for (size_t index = 0; index < input.length && remaining > 0; index++) {
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
 * @brief Recursively converts an AST node to DOT format representation.
 *
 * This function generates a GraphViz DOT representation of a syntax tree node and its children.
 * It handles:
 * - Node labeling with type names and values
 * - Proper escaping of special characters in values
 * - Child relationships with optional tags
 * - Automatic ID generation for nodes
 *
 * @param node The node to convert (must not be NULL).
 * @param last_node_id Pointer to the last used node ID (will be incremented).
 * @param builder Source builder for accumulating DOT output.
 * @return int The assigned node ID.
 */
static int node_to_dot(const node_t* node, int* last_node_id, source_builder_t* builder) {
    int id = *(++last_node_id);
    const wchar_t* name = node->vtbl->type_name;
    string_value_t value = node->vtbl->get_data(node);
    if (value.length > 0) {
        string_value_t formatted_value = trim_and_escape_html_entities(value);
        add_formatted_line_of_source_code(
            builder,
            1,
            format_string(
                L"node_%d [label = <%s<font color='red'>%s</font>>];",
                id,
                name,
                formatted_value.data
            )
        );
        if (formatted_value.should_free) {
            FREE(formatted_value.data);
        }
    } else {
        add_formatted_line_of_source_code(
            builder,
            1,
            format_string(
                L"node_%d [label = \"%s\"];",
                id,
                name
            )
        );
    }
    size_t count = node->vtbl->get_child_count(node);
    for (size_t index = 0; index < count; index++) {
        int child_id = node_to_dot(node->vtbl->get_child(node, index), last_node_id, builder);
        const wchar_t* tag = node->vtbl->get_child_tag(node, index);
        if (tag == NULL) {
            add_formatted_line_of_source_code(
                builder,
                1,
                format_string(
                    L"node_%d -> node_%d [label = \"%zu\"];",
                    id,
                    child_id,
                    index
                )
            );
        } else {
            add_formatted_line_of_source_code(
                builder,
                1,
                format_string(
                    L"node_%d -> node_%d [label = \"%s\"];",
                    id,
                    child_id,
                    tag
                )
            );
        }
    }
    return id;
}

bool generate_image(const node_t* root, const char *graph_output_file) {
    return false;
}