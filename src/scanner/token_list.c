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

void append_token_to_neighbors(token_list_t *neighbors, token_t *token) {
    assert(neighbors != NULL);
    assert(token != NULL);
    assert(token->neighbors == NULL);
    assert(token->left_neighbor == NULL);
    assert(token->right_neighbor == NULL);

    if (neighbors->size == 0) {
        neighbors->head = token;
    } else {
        assert(neighbors->tail != NULL);
        token->left_neighbor = neighbors->tail;
        neighbors->tail->right_neighbor = token;
    }
    neighbors->tail = token;
    token->neighbors = neighbors;
    neighbors->size++;
}

void append_token_to_group(token_list_t *group, token_t *token) {
    assert(group != NULL);
    assert(token != NULL);
    assert(token->group == NULL);
    assert(token->previous_in_group == NULL);
    assert(token->next_in_group == NULL);

    if (group->size == 0) {
        group->head = token;
    } else {
        assert(group->tail != NULL);
        token->previous_in_group = group->tail;
        group->tail->next_in_group = token;
    }
    group->tail = token;
    token->group = group;
    group->size++;
}

void prepend_token_to_neighbors(token_list_t *neighbors, token_t *token) {
    assert(neighbors != NULL);
    assert(token != NULL);
    assert(token->neighbors == NULL);
    assert(token->left_neighbor == NULL);
    assert(token->right_neighbor == NULL);

    if (neighbors->size == 0) {
        neighbors->tail = token;
    } else {
        assert(neighbors->head != NULL);
        token->right_neighbor = neighbors->head;
        neighbors->head->left_neighbor = token;
    }
    neighbors->head = token;
    token->neighbors = neighbors;
    neighbors->size++;
}

void remove_token(token_t *token) {
    assert(token != NULL);
    assert(token->neighbors != NULL);

    token_list_t *neighbors = token->neighbors;

    if (neighbors->size == 1) {
        neighbors->head = NULL;
        neighbors->tail = NULL;
    } else {
        if (token == neighbors->head) {
            neighbors->head = token->right_neighbor;
            neighbors->head->left_neighbor = NULL;
        } else if (token == neighbors->tail) {
            neighbors->tail = token->left_neighbor;
            neighbors->tail->right_neighbor = NULL;
        } else {
            token->left_neighbor->right_neighbor = token->right_neighbor;
            token->right_neighbor->left_neighbor = token->left_neighbor;
        }
    }
    token->neighbors = NULL;
    token->left_neighbor = NULL;
    token->right_neighbor = NULL;
    neighbors->size--;

    if (token->group != NULL) {
        token_list_t *group = token->group;

        if (group->size == 1) {
            group->head = NULL;
            group->tail = NULL;
        } else {
            if (token == group->head) {
                group->head = token->next_in_group;
                group->head->previous_in_group = NULL;
            } else if (token == group->tail) {
                group->tail = token->previous_in_group;
                group->tail->next_in_group = NULL;
            } else {
                token->previous_in_group->next_in_group = token->next_in_group;
                token->next_in_group->previous_in_group = token->previous_in_group;
            }
        }
        token->group = NULL;
        token->previous_in_group = NULL;
        token->next_in_group = NULL;
        group->size--;
    }
}

void replace_token_in_neighbors(token_t *old_token, token_t *new_token) {
    assert(old_token != NULL);
    assert(new_token != NULL);
    assert(old_token->neighbors != NULL);
    assert(new_token->neighbors == NULL);
    assert(new_token->left_neighbor == NULL);
    assert(new_token->right_neighbor == NULL);

    token_list_t *neighbors = old_token->neighbors;
    assert(neighbors->head != NULL);
    assert(neighbors->tail != NULL);

    if (old_token == neighbors->head) {
        new_token->right_neighbor = neighbors->head->right_neighbor;
        if (neighbors->head->right_neighbor) {
            neighbors->head->right_neighbor->left_neighbor = new_token;
        }
        neighbors->head = new_token;
    } else if (old_token == neighbors->tail) {
        new_token->left_neighbor = neighbors->tail->left_neighbor;
        neighbors->tail->left_neighbor->right_neighbor = new_token;
        neighbors->tail = new_token;
    } else {
        old_token->left_neighbor->right_neighbor = new_token;
        old_token->right_neighbor->left_neighbor = new_token;
        new_token->left_neighbor = old_token->left_neighbor;
        new_token->right_neighbor = old_token->right_neighbor;
    }

    old_token->neighbors = NULL;
    old_token->left_neighbor = NULL;
    old_token->right_neighbor = NULL;

    new_token->neighbors = neighbors;
}
