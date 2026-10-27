/** @file transfer.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Operation-local identity maps for shallow storage and function transfer.
 */
#include "transfer.h"

#include "lib/memory/allocate.h"

typedef struct {
    void *source;
    void *destination;
} identity_pair_t;

typedef struct {
    object_t *source_storage;
    field_cell_t *source;
    object_t *destination_storage;
    field_cell_t *destination;
} cell_pair_t;

struct transfer_t {
    process_t *process;
    vector_t *storage;
    vector_t *domains; /**< Owns destination domain references. */
    vector_t *cells;
    vector_t *functions;    /**< Owns new function references until cells retain them. */
    vector_t *environments; /**< Owns lexical views used only by transferred functions. */
    object_t *receiver;     /**< Borrowed original flat receiver; NULL for clone. */
};

static void *mapped(const vector_t *map, const void *source) {
    for (size_t i = 0; i < map->size; i++) {
        identity_pair_t *pair = map->data[i];
        if (pair->source == source)
            return pair->destination;
    }
    return NULL;
}

static void add_pair(vector_t *map, void *source, void *destination) {
    identity_pair_t *pair = ALLOC(sizeof(*pair));
    *pair = (identity_pair_t){source, destination};
    append_to_vector(map, pair);
}

transfer_t *create_transfer(process_t *process) {
    transfer_t *transfer = ALLOC(sizeof(*transfer));
    *transfer = (transfer_t){process,
                             create_vector(),
                             create_vector(),
                             create_vector(),
                             create_vector(),
                             create_vector(),
                             NULL};
    return transfer;
}

void transfer_add_storage(transfer_t *transfer, object_t *source, object_t *destination) {
    add_pair(transfer->storage, source, destination);
}

void transfer_add_domain(transfer_t *transfer,
                         access_domain_t *source,
                         access_domain_t *destination) {
    add_pair(transfer->domains, source, retain_access_domain(destination));
}

object_t *transfer_storage(const transfer_t *transfer, object_t *source) {
    object_t *destination = mapped(transfer->storage, source);
    return destination ? destination : source;
}

void configure_flat_transfer(transfer_t *transfer, object_t *receiver) {
    transfer->receiver = receiver;
}

object_t *transfer_closure(transfer_t *transfer, object_t *source) {
    object_t *previous_storage = get_transfer_view_storage(source);
    object_t *storage = previous_storage ? previous_storage : source;
    object_t *target = transfer_storage(transfer, storage);
    if (target == storage)
        return source;
    if (!previous_storage && !transfer->receiver)
        return target;
    object_t *view = mapped(transfer->environments, source);
    if (view)
        return view;
    object_array_t parents = get_object_prototypes(source);
    size_t inherited = previous_storage ? 1 : get_inheritance_prototypes(source).size;
    if (inherited == parents.size)
        return target;
    size_t count = 1 + parents.size - inherited;
    object_t **items = ALLOC(count * sizeof(*items));
    items[0] = target;
    for (size_t i = 1; i < count; i++)
        items[i] = parents.items[inherited + i - 1];
    view = create_user_defined_object_in_file(transfer->process,
                                              (object_array_t){items, count},
                                              get_object_module(source),
                                              get_object_file_id(source));
    set_inheritance_prototype_count(view, 0);
    mark_transfer_view(view);
    FREE(items);
    add_pair(transfer->environments, source, view);
    return view;
}

access_domain_t *transfer_domain(transfer_t *transfer, access_domain_t *source) {
    access_domain_t *destination = mapped(transfer->domains, source);
    if (!destination) {
        destination = create_access_domain(get_access_domain_module(source));
        add_pair(transfer->domains, source, destination);
    }
    return destination;
}

void transfer_add_cell(transfer_t *transfer,
                       object_t *source_storage,
                       field_cell_t *source,
                       object_t *destination_storage,
                       field_cell_t *destination) {
    cell_pair_t *pair = ALLOC(sizeof(*pair));
    *pair = (cell_pair_t){source_storage, source, destination_storage, destination};
    append_to_vector(transfer->cells, pair);
}

static void copy_grants(const transfer_t *transfer, vector_t *destination, const vector_t *source) {
    /* New slots win lexical/private lookup; old grants remain for explicit original references. */
    for (size_t i = 0; i < source->size; i++) {
        access_domain_t *domain = mapped(transfer->domains, source->data[i]);
        if (domain)
            append_to_vector(destination, retain_access_domain(domain));
    }
    for (size_t i = 0; i < source->size; i++)
        append_to_vector(destination, retain_access_domain(source->data[i]));
}

void transfer_function_access(const transfer_t *transfer,
                              function_access_t *destination,
                              const function_access_t *source) {
    destination->file_id = source->file_id;
    function_access_t effective;
    init_function_access(&effective, source->module);
    inherit_function_access(&effective, source);
    if (transfer->receiver)
        add_inherited_receiver_access(&effective, transfer->receiver, source);
    copy_grants(transfer, destination->domains, effective.domains);
    copy_grants(transfer, destination->protected_domains, effective.protected_domains);
    /* Capture before adding merged scopes: one descriptor can identify several factory cells. */
    for (size_t i = 0; i < effective.captures->size; i++) {
        field_capture_t *capture = effective.captures->data[i];
        object_t *storage = capture->storage;
        field_cell_t *cell = capture->cell;
        for (size_t j = 0; j < transfer->cells->size; j++) {
            cell_pair_t *pair = transfer->cells->data[j];
            if (pair->source_storage == storage && pair->source == cell) {
                storage = pair->destination_storage;
                cell = pair->destination;
                break;
            }
        }
        add_function_field_capture(destination, storage, cell);
    }
    for (size_t i = 0; i < source->lexical_scopes->size; i++)
        add_lexical_scope(destination, transfer_storage(transfer, source->lexical_scopes->data[i]));
    clear_function_access(&effective, true);
}

void finish_transfer(transfer_t *transfer) {
    for (size_t i = 0; i < transfer->cells->size; i++) {
        cell_pair_t *pair = transfer->cells->data[i];
        object_t *value = pair->source->object;
        if (get_function_access(value) && !mapped(transfer->functions, value))
            add_pair(transfer->functions,
                     value,
                     create_transferred_function(transfer->process, value));
    }
    for (size_t i = 0; i < transfer->functions->size; i++) {
        identity_pair_t *pair = transfer->functions->data[i];
        link_transferred_function(pair->destination, pair->source, transfer);
    }
    for (size_t i = 0; i < transfer->cells->size; i++) {
        cell_pair_t *pair = transfer->cells->data[i];
        object_t *value = mapped(transfer->functions, pair->source->object);
        if (!value)
            continue;
        /* Trusted initialization of unpublished cells, including constants. */
        INCREF(value);
        if (pair->destination->object != pair->destination_storage)
            DECREF(pair->destination->object);
        pair->destination->object = value;
    }
}

void destroy_transfer(transfer_t *transfer) {
    for (size_t i = 0; i < transfer->domains->size; i++)
        release_access_domain(((identity_pair_t *)transfer->domains->data[i])->destination);
    for (size_t i = 0; i < transfer->functions->size; i++) {
        object_t *function = ((identity_pair_t *)transfer->functions->data[i])->destination;
        DECREF(function);
    }
    for (size_t i = 0; i < transfer->environments->size; i++) {
        object_t *view = ((identity_pair_t *)transfer->environments->data[i])->destination;
        DECREF(view);
    }
    vector_t *maps[] = {transfer->storage,
                        transfer->domains,
                        transfer->cells,
                        transfer->functions,
                        transfer->environments};
    for (size_t m = 0; m < sizeof(maps) / sizeof(*maps); m++) {
        for (size_t i = 0; i < maps[m]->size; i++)
            FREE(maps[m]->data[i]);
        destroy_vector(maps[m]);
    }
    FREE(transfer);
}
