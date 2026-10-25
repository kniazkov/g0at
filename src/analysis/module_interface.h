/** @file module_interface.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Read validated module contracts without executing or analyzing dependency code.
 */
#pragma once
#include "lattice.h"
#include "yyjson.h"

typedef struct abstract_state_t abstract_state_t;

/** Borrowed immutable metadata. The resolver is restricted to this module's
 * verified reachable graph; it must not search files or execute code. */
typedef struct module_interface_t {
    yyjson_val *metadata;
    yyjson_val *(*resolve)(void *context, const char *name, const char *version);
    void *context;
} module_interface_t;

/** Code identity only: no closure instance or captured values are represented. */
typedef struct imported_function_t {
    const char *module, *version;
    uint32_t function_id;
    yyjson_val *record;
} imported_function_t;

/** Arena-owned scope provenance; general object joins may discard this refinement. */
const lattice_element_t *make_module_scope_element(arena_t *arena,
                                                   const module_interface_t *interface);
/** Distinguish the arena-owned module refinement from the general object singleton. */
bool is_module_scope_element(const lattice_element_t *value);
/** Unknown/mutable exports remain TOP. This lookup never substitutes runtime access. */
const lattice_element_t *
module_interface_member(const lattice_element_t *object, string_view_t name, arena_t *arena);
/** Convert validated facts conservatively to the existing lattice. */
const lattice_element_t *module_interface_facts(yyjson_val *facts, arena_t *arena);
/** Use only summaries whose conditions cover all possible argument values. */
const lattice_element_t *interpret_imported_call(const imported_function_t *function,
                                                 const lattice_element_t *const *args,
                                                 size_t count,
                                                 abstract_state_t *state,
                                                 uint32_t *effects);

/** A granted private slot may precede the receiver's ordinary exported property. */
const lattice_element_t *module_interface_member_at(const lattice_element_t *object,
                                                    string_view_t name,
                                                    arena_t *arena,
                                                    const struct node_t *observer);
