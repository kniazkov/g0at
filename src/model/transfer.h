/** @file transfer.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Two-phase transfer of cells and directly held methods, without deep copying values.
 */
#pragma once
#include "access.h"

typedef struct transfer_t transfer_t;

/** @brief Source and destination storage must remain alive until the operation finishes. */
transfer_t *create_transfer(process_t *process);
void transfer_add_storage(transfer_t *transfer, object_t *source, object_t *destination);
/** @brief Maps a domain independently of storage: several domains may share one destination object.
 */
void transfer_add_domain(transfer_t *transfer,
                         access_domain_t *source,
                         access_domain_t *destination);
access_domain_t *transfer_domain(transfer_t *transfer, access_domain_t *source);
void transfer_add_cell(transfer_t *transfer,
                       object_t *source_storage,
                       field_cell_t *source,
                       object_t *destination_storage,
                       field_cell_t *destination);
/** @brief Allocates every directly held function before connecting environments and cell values. */
void finish_transfer(transfer_t *transfer);
void destroy_transfer(transfer_t *transfer);
object_t *transfer_storage(const transfer_t *transfer, object_t *source);
void transfer_function_access(const transfer_t *transfer,
                              function_access_t *destination,
                              const function_access_t *source);

/** @brief Internal dynamic-function allocation and environment linking; builtins are unchanged. */
object_t *create_transferred_function(process_t *process, object_t *source);
void link_transferred_function(object_t *destination, object_t *source, const transfer_t *transfer);
