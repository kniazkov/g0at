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
    assert(token->left == NULL);
    assert(token->right == NULL);

    if (neighbors->count == 0) {
        neighbors->first = token;
    } else {
        assert(neighbors->last != NULL);
        token->left = neighbors->last;
        neighbors->last->right = token;
    }
    neighbors->last = token;
    token->neighbors = neighbors;
    neighbors->count++;
}

void append_token_to_group(token_list_t *group, token_t *token) {
    assert(group != NULL);
    assert(token != NULL);
    assert(token->group == NULL);
    assert(token->previous_in_group == NULL);
    assert(token->next_in_group == NULL);

    if (group->count == 0) {
        group->first = token;
    } else {
        assert(group->last != NULL);
        token->previous_in_group = group->last;
        group->last->next_in_group = token;
    }
    group->last = token;
    token->group = group;
    group->count++;
}

void prepend_token_to_neighbors(token_list_t *neighbors, token_t *token) {
    assert(neighbors != NULL);
    assert(token != NULL);
    assert(token->neighbors == NULL);
    assert(token->left == NULL);
    assert(token->right == NULL);

    if (neighbors->count == 0) {
        neighbors->last = token;
    } else {
        assert(neighbors->first != NULL);
        token->right = neighbors->first;
        neighbors->first->left = token;
    }
    neighbors->first = token;
    token->neighbors = neighbors;
    neighbors->count++;
}

void remove_token_from_group(token_t *token) {
    if (token->group != NULL) {
        token_list_t *group = token->group;

        if (group->count == 1) {
            group->first = NULL;
            group->last = NULL;
        } else {
            if (token == group->first) {
                group->first = token->next_in_group;
                group->first->previous_in_group = NULL;
            } else if (token == group->last) {
                group->last = token->previous_in_group;
                group->last->next_in_group = NULL;
            } else {
                token->previous_in_group->next_in_group = token->next_in_group;
                token->next_in_group->previous_in_group = token->previous_in_group;
            }
        }
        token->group = NULL;
        token->previous_in_group = NULL;
        token->next_in_group = NULL;
        group->count--;
    }
}

void remove_token(token_t *token) {
    assert(token != NULL);
    assert(token->neighbors != NULL);

    token_list_t *neighbors = token->neighbors;

    if (neighbors->count == 1) {
        neighbors->first = NULL;
        neighbors->last = NULL;
    } else {
        if (token == neighbors->first) {
            neighbors->first = token->right;
            neighbors->first->left = NULL;
        } else if (token == neighbors->last) {
            neighbors->last = token->left;
            neighbors->last->right = NULL;
        } else {
            token->left->right = token->right;
            token->right->left = token->left;
        }
    }
    token->neighbors = NULL;
    token->left = NULL;
    token->right = NULL;
    neighbors->count--;

    remove_token_from_group(token);
}

void replace_token(token_t *old_token, token_t *new_token) {
    assert(old_token != NULL);
    assert(new_token != NULL);
    assert(old_token->neighbors != NULL);
    assert(new_token->neighbors == NULL);
    assert(new_token->left == NULL);
    assert(new_token->right == NULL);

    token_list_t *neighbors = old_token->neighbors;
    assert(neighbors->first != NULL);
    assert(neighbors->last != NULL);

    if (old_token == neighbors->first) {
        new_token->right = neighbors->first->right;
        if (neighbors->first->right) {
            neighbors->first->right->left = new_token;
        }
        neighbors->first = new_token;
    } else if (old_token == neighbors->last) {
        new_token->left = neighbors->last->left;
        neighbors->last->left->right = new_token;
        neighbors->last = new_token;
    } else {
        old_token->left->right = new_token;
        old_token->right->left = new_token;
        new_token->left = old_token->left;
        new_token->right = old_token->right;
    }

    old_token->neighbors = NULL;
    old_token->left = NULL;
    old_token->right = NULL;

    new_token->neighbors = neighbors;

    remove_token_from_group(old_token);
}
