/**
 * @file model_status.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Status codes for model operations
 */

#pragma once

/**
 * @enum model_status_t
 * @brief Status codes returned by model operations
 */
typedef enum {
    MSTAT_OK = 0, /**< Operation completed successfully */
    MSTAT_IMMUTABLE_OBJECT, /**< Target object is immutable */
    MSTAT_PROPERTY_ALREADY_EXISTS, /**< Property already exists */
    MSTAT_PROPERTY_NOT_FOUND, /**< Property not found */
    MSTAT_PROPERTY_IS_CONSTANT, /**< Property is constant and cannot be modified */
} model_status_t;
