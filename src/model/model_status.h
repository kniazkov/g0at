/**
 * @file model_status.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Status codes for model operations
 */

#pragma once

/** @brief Status codes returned by model operations */
typedef enum {
    MSTAT_OK = 0,                  /**< Operation completed successfully */
    MSTAT_IMMUTABLE_OBJECT,        /**< Target object is immutable */
    MSTAT_PROPERTY_ALREADY_EXISTS, /**< Property already exists */
    MSTAT_PROPERTY_NOT_FOUND,      /**< Property not found */
    MSTAT_PROPERTY_IS_CONSTANT,    /**< Property is constant and cannot be modified */
    MSTAT_ACCESS_DENIED,           /**< The winning declaration is not accessible */
    MSTAT_UNINITIALIZED,           /**< A known declaration has not executed yet */
    MSTAT_FAILED_FILE,             /**< File scope initialization failed; payload is retained */
    MSTAT_FOREIGN_OBJECT,          /**< Another module owns the object being extended */
} model_status_t;
