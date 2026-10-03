/** @file c_module.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Deterministic inventory of proven numeric function specializations.
 */
#pragma once
#include "c_generation.h"

typedef struct c_module_function_t c_module_function_t;

/** @brief One original call site and its module target; NULL means a missing dependency. */
typedef struct c_module_dependency_t {
    struct c_module_dependency_t *next;
    const node_t *site;
    const c_module_function_t *target;
} c_module_dependency_t;

/** @brief Compilation-time record; available means closed dependencies, not emitter support. */
struct c_module_function_t {
    c_module_function_t *next;
    size_t id;
    size_t function_id;
    const function_summary_t *summary;
    string_view_t name;
    c_module_dependency_t *dependencies;
    c_generation_callee_t *callees;
    bool available;
};

/** @brief Arena-owned inventory borrowing AST and summaries; rebuild after reanalysis. */
typedef struct c_module_t {
    c_module_function_t *head, *tail;
    size_t count, available_count;
} c_module_t;

/** @brief Inventories original syntax in preorder and signatures in numeric-type order. */
c_module_t *create_c_module(arena_t *arena, const node_t *root);

/** @brief Finds by function identity and exact parameter types, including summary snapshots. */
const c_module_function_t *c_module_find(const c_module_t *module,
                                         const function_summary_t *summary);
