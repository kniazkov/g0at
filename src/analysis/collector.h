/**
 * @file collector.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Arena-backed observations of abstract interpretation.
 */
#pragma once

#include "lib/arena.h"
#include "lib/value.h"

#include <stdint.h>

typedef struct node_t node_t;
typedef struct declarator_t declarator_t;
typedef struct lattice_element_t lattice_element_t;
typedef struct analysis_event_t analysis_event_t;
typedef struct function_summary_t function_summary_t;
typedef struct function_call_graph_t function_call_graph_t;

typedef enum {
    ANALYSIS_EVENT_ANY = 0, /**< Query wildcard; never recorded. */
    ANALYSIS_VALUE_WRITE,
    ANALYSIS_STATE_JOIN,
    ANALYSIS_DECLARATION_SUMMARY,
    ANALYSIS_UNREACHABLE,      /**< Subtree root; declarator and value are NULL. */
    ANALYSIS_NODE_FLAGS,       /**< Final proof snapshot, including zero (no proof). */
    ANALYSIS_FUNCTION_SUMMARY, /**< Function-body facts, independent of later analysis. */
    ANALYSIS_CALL_EDGE,        /**< Call-site target; NULL callee means unknown or graph limit. */
    ANALYSIS_CALL_GROUP        /**< Strongly connected component membership. */
} analysis_event_kind_t;

/** @brief One observation, not a mutable reference to an abstract state. */
struct analysis_event_t {
    analysis_event_t *next;
    size_t sequence; /**< One-based chronological number. */
    analysis_event_kind_t kind;
    const node_t *node;
    const declarator_t *declarator;
    const lattice_element_t *value;
    const function_summary_t *function_summary; /**< Snapshot for ANALYSIS_FUNCTION_SUMMARY. */
    const function_summary_t *callee_summary;
    size_t caller_id, callee_id;
    size_t component, component_size;
    bool recursive, complete, limited;
    uint32_t flags; /**< Snapshot for ANALYSIS_NODE_FLAGS, independent of later node changes. */
    const char *file_name;
    size_t row;
    size_t column;
};

/**
 * @brief Collector and list entries belong to the supplied arena; no separate destruction.
 * Nodes, declarations, immutable lattice values, and filenames are borrowed. Their arenas
 * and source storage must outlive queries and formatting. Positions are captured on insertion
 * from the node or its nearest ancestor with a source position.
 */
typedef struct analysis_collector_t {
    arena_t *arena;
    analysis_event_t *head;
    analysis_event_t *tail;
    size_t count;
} analysis_collector_t;

/** @brief All selectors must match; zero positions and kind are wildcards. Flags use flags_mask. */
typedef struct {
    analysis_event_kind_t kind;
    const node_t *node;
    const declarator_t *declarator;
    const char *file_name;
    size_t row;
    size_t column;
    uint32_t flags_mask; /**< Nonzero selects flag events with matching masked bits. */
    uint32_t flags;
} analysis_event_query_t;

/** @brief Creates an empty collector in a non-NULL arena. */
analysis_collector_t *create_analysis_collector(arena_t *arena);

/**
 * @brief Appends in O(1); NULL collector disables recording.
 * Structural events require a node and NULL declaration/value; value events require both.
 * Function summaries use add_function_summary_event().
 */
const analysis_event_t *add_analysis_event(analysis_collector_t *collector,
                                           analysis_event_kind_t kind,
                                           const node_t *node,
                                           const declarator_t *declarator,
                                           const lattice_element_t *value);

/** @brief Appends an isolated signature snapshot; NULL collector disables recording. */
const analysis_event_t *add_function_summary_event(analysis_collector_t *collector,
                                                   const function_summary_t *summary);

/** @brief Appends graph edges and component snapshots; NULL collector disables recording. */
void add_call_graph_events(analysis_collector_t *collector, const function_call_graph_t *graph);

/**
 * @brief Finds the next matching event in O(n); NULL query matches everything.
 * Starts at head when after is NULL, otherwise after must belong to this collector.
 */
const analysis_event_t *find_analysis_event(const analysis_collector_t *collector,
                                            const analysis_event_t *after,
                                            const analysis_event_query_t *query);

/** @brief Finds the last recorded match, not a join or a fixed-point result. */
const analysis_event_t *find_last_analysis_event(const analysis_collector_t *collector,
                                                 const analysis_event_query_t *query);

/** @brief Formats chronological observations; release with FREE_STRING(). NULL means empty. */
string_value_t analysis_collector_to_text(const analysis_collector_t *collector);
