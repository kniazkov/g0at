/**
 * @file properties.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Conservative structural and registered-specialization proof caches.
 */
#pragma once

#include "collector.h"

/** @brief Classifies children before parents, after reachability has supplied pointwise facts. */
void classify_node_properties(node_t *root, analysis_collector_t *collector);
