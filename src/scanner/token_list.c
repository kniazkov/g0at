/**
 * @file token_list.c
 * @copyright 2025 Ivan Kniazkov
 * @brief Provides functions for manipulating tokens in token lists.
 *
 * This file contains the implementation of functions that allow manipulation of tokens 
 * within a doubly linked list. The functions include:
 * - Adding tokens to the list (both at the beginning and at the end).
 * - Removing tokens from the list.
 * - Replacing one token with another in the list.

 * These operations are essential for manipulating lists of tokens during lexical analysis,
 * tokenization, or syntactic parsing phases of the program.
 */

#include <assert.h>

#include "token.h"

void append_token_to_list(token_list_t *list, token_t *token) {
    assert(list != NULL);
    assert(token != NULL);
    assert(token->list == NULL);
    assert(token->previous == NULL);
    assert(token->next == NULL);

    if (list->size == 0) {
        list->head = token;
    } else {
        assert(list->tail != NULL);
        token->previous = list->tail;
        list->tail->next = token;
    }
    list->tail = token;
    token->list = list;
    list->size++;
}

void prepend_token_to_list(token_list_t *list, token_t *token) {
    assert(list != NULL);
    assert(token != NULL);
    assert(token->list == NULL);
    assert(token->previous == NULL);
    assert(token->next == NULL);

    if (list->size == 0) {
        list->tail = token;
    } else {
        assert(list->head != NULL);
        token->next = list->head;
        list->head->previous = token;
    }
    list->head = token;
    token->list = list;
    list->size++;
}

void remove_token(token_t *token) {
    assert(token != NULL);
    assert(token->list != NULL);

    token_list_t *list = token->list;

    if (list->size == 1) {
        list->head = NULL;
        list->tail = NULL;
    } else {
        if (token == list->head) {
            list->head = token->next;
            list->head->previous = NULL;
        }
        else if (token == list->tail) {
            list->tail = token->previous;
            list->tail->next = NULL;
        }
        else {
            token->previous->next = token->next;
            token->next->previous = token->previous;
        }
    }
    token->list = NULL;
    token->previous = NULL;
    token->next = NULL;
    list->size--;
}

void replace_token(token_t *old_token, token_t *new_token) {
    assert(old_token != NULL);
    assert(new_token != NULL);
    assert(old_token->list != NULL);
    assert(new_token->list == NULL);
    assert(new_token->previous == NULL);
    assert(new_token->next == NULL);

    token_list_t *list = old_token->list;
    assert(list->head != NULL);
    assert(list->tail != NULL);

    if (old_token == list->head) {
        new_token->next = list->head->next;
        if (list->head->next) {
            list->head->next->previous = new_token;
        }
        list->head = new_token;
    } else if (old_token == list->tail) {
        new_token->previous = list->tail->previous;
        list->tail->previous->next = new_token;
        list->tail = new_token;
    } else {
        old_token->previous->next = new_token;
        old_token->next->previous = new_token;
        new_token->previous = old_token->previous;
        new_token->next = old_token->next;
    }
    old_token->list = NULL;
    old_token->previous = NULL;
    old_token->next = NULL;
    new_token->list = list;
}
