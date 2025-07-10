/**
 * @file path.h
 * @copyright 2025 Ivan Kniazkov
 * @brief Platform-independent path manipulation structures and functions.
 *
 * This file provides cross-platform path handling functionality including:
 * - Path normalization with system-specific separators
 * - Component extraction (directory, filename, extension)
 * - Path joining operations
 */

#pragma once

/**
 * @struct path_t
 * @brief Structure representing a filesystem path with decomposed components.
 *
 * All string fields are null-terminated and managed by the structure.
 */
typedef struct {
    char* normal_path;  /**< Normalized path with system separators */
    char* full_path;    /**< Full absolute path with system separators */
    char* dir_name;     /**< Directory portion or NULL */
    char* file_name;    /**< Filename with extension or NULL */
    char* extension;    /**< File extension without dot or NULL */
} path_t;

/**
 * @brief Creates a new path_t structure from input path.
 * @param input Input filesystem path.
 * @return New path_t instance (never NULL).
 */
path_t *create_path(const char *input);

/**
 * @brief Frees all resources associated with path_t.
 * @param path Path object to free.
 */
void free_path(path_t *path);
