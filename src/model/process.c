/**
 * @file process.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Implements functions for managing processes in the Goat programming language.
 */

#include "process.h"
#include "thread.h"
#include "context.h"
#include "lib/allocate.h"
#include "object_list.h"

/** @brief ID of the last created process. */
static uint64_t last_process_id = 0;

process_t *create_process() {
    process_t *process = (process_t *)CALLOC(sizeof(process_t));
    process->id = ++last_process_id;
    init_object_list(&process->objects);
    init_object_list(&process->integers);
    init_object_list(&process->real_numbers);
    init_object_list(&process->dynamic_strings);
    init_object_list(&process->user_defined_objects);
    create_thread(process, create_context(process, get_root_context(), NULL));
    return process;
}

/** @brief Destroys all objects in the given list by calling their release function. */
static void destroy_all_objects_in_the_list(object_list_t *list) {
    object_t *object = list->head;
    while (object) {
        object_t *next = object->next;
        release_object(object);
        object = next;
    }
}

void destroy_process(process_t *process) {
    while(process->main_thread) {
        destroy_thread(process->main_thread);
    }
    destroy_all_objects_in_the_list(&process->objects);
    destroy_all_objects_in_the_list(&process->integers);
    destroy_all_objects_in_the_list(&process->real_numbers);
    destroy_all_objects_in_the_list(&process->dynamic_strings);
    destroy_all_objects_in_the_list(&process->user_defined_objects);
    FREE(process->string_cache);
    FREE(process);
}
