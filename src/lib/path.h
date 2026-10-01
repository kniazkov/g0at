/**
 * @file path.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Platform-independent path manipulation structures and functions.
 */

#pragma once

/**
 * @brief A filesystem path with decomposed components.
 *
 * All string fields are null-terminated and managed by the structure.
 */
typedef struct {
    char* normal_path;  /**< Normalized path with system separators */
    char* full_path;    /**< Absolute path, or an owned copy of normal_path if resolution fails */
    char* dir_name;     /**< Directory portion or NULL */
    char* file_name;    /**< Filename with extension or NULL */
    char* extension;    /**< File extension without dot or NULL */
} path_t;

/**
 * @brief Creates a new path_t structure from input path.
 * @return New path_t instance (never NULL).
 */
path_t *create_path(const char *input);

/** @brief Frees all resources associated with path_t. */
void free_path(path_t *path);
