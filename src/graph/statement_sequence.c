/**
 * @file statement_sequence.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Helpers for working with ordered lists of statement nodes.
 */

#include "statement_sequence.h"
#include "node.h"
#include "statement.h"
#include "lib/allocate.h"
#include "lib/string_ext.h"
#include "lib/linked_list.h"

bool insert_statement_to_list_before(list_t *list, node_t *new_child, node_t *before_child) {
    if (!is_statement(new_child->vtbl->type)) {
        return false;
    }
    list_item_t *item = list->head;
    while (item) {
        if (item->value.ptr == before_child) {
            break;
        }
        item = item->next;
    }
    if (!item) {
        return false;
    }
    insert_item_to_linked_list_before_existing(
        list,
        item,
        (value_t){ .ptr = new_child }
    );
    return true;
}

string_value_t generate_goat_code_from_statement_list(list_t *list,
        string_builder_t *builder, bool brackets) {
    if (list->size == 0) {
        if (builder->length > 0) { // there is already a header
            return append_static_string(builder, L"{ }");
        } else if (brackets) {
            return STATIC_STRING(L"{ }");
        } else {
            return EMPTY_STRING_VALUE;
        }
    }

    if (brackets) {
        append_char(builder, L'{');
    }
    bool has_previous = false;
    list_item_t *item = list->head;
    string_value_t result;

    while (item) {
        if (has_previous) {
            append_char(builder, L' ');
        }
        has_previous = true;

        statement_t *stmt = (statement_t*)item->value.ptr;
        string_value_t stmt_as_string = generate_goat_code_from_statement(stmt);
        result = append_string_value(builder, stmt_as_string);
        FREE_STRING(stmt_as_string);

        item = item->next;
    }

    if (brackets) {
        result = append_char(builder, L'}');
    }
    return result;
}
