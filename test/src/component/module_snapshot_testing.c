/** @file module_snapshot_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Whole-snapshot lifetime, immutable image and memory-only VM checks.
 */
#include "lib/files/file_system.h"
#include "lib/hash/sha256.h"
#include "lib/memory/allocate.h"
#include "model/thread.h"
#include "vm/gc.h"
#include "vm/module_snapshot.h"
#include "vm/vm.h"

#ifdef NDEBUG
#    undef NDEBUG /* Contract checks and fixture calls stay active in Release. */
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <fcntl.h>
#    include <io.h>
#endif

static void checked(const module_validation_diagnostic_t *d, void *context) {
    if (d->check == MODULE_CHECK_INPUT && d->state == MODULE_CHECK_PASSED)
        (*(size_t *)context)++;
}

static void digest(const void *data, size_t size, char result[65]) {
    uint8_t bytes[SHA256_SIZE];
    sha256(data, size, bytes);
    for (size_t i = 0; i < SHA256_SIZE; ++i)
        sprintf(result + 2 * i, "%02x", bytes[i]);
}

static void borrowed_views(const bytecode_t *source) {
    bytecode_t *first = borrow_bytecode_image(source);
    assert(first && first->borrowed_buffer && first->buffer == source->buffer);
    first->declaration_unit = create_module_identity();
    first->native_functions = CALLOC(first->instructions_count * sizeof(*first->native_functions));
    bytecode_t *second = borrow_bytecode_image(first);
    assert(second && second->buffer == source->buffer && second->borrowed_buffer);
    assert(!second->declaration_unit && !second->native_functions);
    second->declaration_unit = create_module_identity();
    assert(second->declaration_unit != first->declaration_unit);
    assert(!source->declaration_unit && !source->native_functions);
    for (size_t i = 0; i < source->instructions_count; ++i) {
        assert(bind_bytecode_native_function(first, i, NULL)
               == (source->instructions[i].opcode == FUNC));
        assert(!get_bytecode_native_function(first, i));
    }
    free_bytecode(first);
    assert(validate_bytecode_image(second));
    assert(second->instructions == source->instructions);
    free_bytecode(second);
}

static int execute(module_snapshot_t *snapshot, module_files_t *files, module_file_t *file) {
    process_t *process = module_snapshot_process(snapshot);
    thread_t *thread = process->main_thread;
    context_t *caller = thread->context;
    context_t *context = create_module_file_context(file, caller);
    assert(context && context->data == module_file_scope(file));
    thread->context = context;
    thread->instr_id = module_file_entry(file);
    int status = run(process, module_files_bytecode(files));
    assert(!thread->data_stack->size);
    assert(!process->string_cache && !process->string_cache_size);
    if (!status) {
        assert(thread->context == context);
        assert(destroy_context(context) == caller);
        thread->context = caller;
    } else {
        assert(thread->context == get_root_context());
        assert(thread->exception.value);
        clear_exception(&thread->exception);
    }
    return status;
}

static void arguments(void) {
    module_snapshot_t *snapshot = (void *)(uintptr_t)1;
    module_graph_t *graph = (void *)(uintptr_t)1;
    assert(load_module_snapshot(NULL, NULL, NULL, &snapshot, &graph) == MODULE_GRAPH_ARGUMENT);
    assert(!snapshot && !graph);
    assert(load_module_snapshot_memory(NULL, 1, "memory", NULL, NULL, &snapshot, &graph)
           == MODULE_GRAPH_ARGUMENT);
    assert(!snapshot && !graph);
    assert(load_module_snapshot(NULL, NULL, NULL, NULL, NULL) == MODULE_GRAPH_ARGUMENT);
    assert(!module_snapshot_count(NULL));
    assert(!module_snapshot_process(NULL));
    assert(!module_snapshot_graph(NULL));
    assert(!module_snapshot_module(NULL, 0));
    assert(!module_snapshot_files(NULL, 0));
    assert(!module_snapshot_dependency_count(NULL, 0));
    assert(module_snapshot_dependency(NULL, 0, 0) == SIZE_MAX);
    assert(module_snapshot_import(NULL, 0, "x", "1") == SIZE_MAX);
    assert(module_snapshot_import(NULL, 0, NULL, NULL) == SIZE_MAX);
    free_module_snapshot(NULL);
    assert(!borrow_bytecode_image(NULL));
}

int main(int argc, char **argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    arguments();
    if (argc == 1) {
        assert(!get_allocated_memory_size());
        return 0;
    }
    assert(argc == 3);
    module_search_t *search = NULL;
    module_search_error_t error = {0};
    assert(!prepare_module_search(argv[1], NULL, NULL, &search, &error));
    void *root_bytes = NULL;
    size_t root_size = 0;
    bool read_root = read_fs_regular_file(argv[1], UINT32_MAX - 1, &root_bytes, &root_size, NULL);
    module_graph_status_t memory_status = MODULE_GRAPH_ARGUMENT;
    if (read_root) {
        module_snapshot_t *memory_snapshot = NULL;
        module_graph_t *memory_diagnostics = NULL;
        memory_status = load_module_snapshot_memory(root_bytes,
                                                    root_size,
                                                    "<unpublished-memory-root>",
                                                    search,
                                                    NULL,
                                                    &memory_snapshot,
                                                    &memory_diagnostics);
        FREE(root_bytes);
        if (!memory_status) {
            assert(memory_snapshot && !memory_diagnostics);
            assert(module_snapshot_count(memory_snapshot));
            borrowed_views(validated_module_bytecode(module_snapshot_module(memory_snapshot, 0)));
        } else {
            assert(!memory_snapshot);
        }
        free_module_snapshot(memory_snapshot);
        free_module_graph(memory_diagnostics);
    }
    process_t *probe = create_process();
    uint64_t prior_process = probe->id;
    destroy_process(probe);
    size_t checks = 0;
    module_validation_options_t options = {SIZE_MAX, SIZE_MAX, checked, &checks};
    module_snapshot_t *snapshot = NULL;
    module_graph_t *diagnostics = NULL;
    bool no_diagnostics = !strcmp(argv[2], "no-diagnostics");
    module_graph_status_t status = load_module_snapshot(argv[1],
                                                        search,
                                                        &options,
                                                        &snapshot,
                                                        no_diagnostics ? NULL : &diagnostics);
    if (read_root)
        assert(memory_status == status);
    free_module_search(search);
    clear_module_search_error(&error);
    /* All input search/path state is gone before the synchronization barrier. */
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *result = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, result);
    yyjson_mut_obj_add_uint(doc, result, "status", status);
    yyjson_mut_obj_add_uint(doc, result, "checks", checks);
    if (status) {
        assert(!snapshot);
        if (!no_diagnostics) {
            assert(status == MODULE_GRAPH_INVALID && diagnostics);
            yyjson_mut_obj_add_uint(doc, result, "issues", module_graph_issue_count(diagnostics));
        }
        probe = create_process();
        assert(probe->id == prior_process + 1); /* No partially prepared runtime. */
        destroy_process(probe);
        free_module_graph(diagnostics);
    } else {
        assert(snapshot && !diagnostics);
        assert(module_snapshot_process(snapshot)->id == prior_process + 1);
        size_t count = module_snapshot_count(snapshot);
        assert(count && checks == count);
        assert(!module_snapshot_files(snapshot, count));
        assert(!module_snapshot_module(snapshot, count));
        assert(!module_snapshot_dependency_count(snapshot, count));
        assert(module_snapshot_dependency(snapshot, count, 0) == SIZE_MAX);
        assert(module_snapshot_import(snapshot, 0, NULL, "1") == SIZE_MAX);
        assert(module_snapshot_import(snapshot, 0, "app", NULL) == SIZE_MAX);
        assert(module_snapshot_import(snapshot, 0, "not-installed", "1") == SIZE_MAX);
        const module_graph_t *graph = module_snapshot_graph(snapshot);
        unsigned char **images = calloc(count, sizeof(*images));
        char **native_hashes = calloc(count, sizeof(*native_hashes));
        assert(images && native_hashes);
        for (size_t i = 0; i < count; ++i) {
            const validated_module_t *module = module_snapshot_module(snapshot, i);
            const bytecode_t *image = validated_module_bytecode(module);
            bytecode_t *code = module_files_bytecode(module_snapshot_files(snapshot, i));
            assert(code != image && code->buffer == image->buffer);
            borrowed_views(image);
            assert(!code->native_functions && !code->declaration_unit);
            images[i] = malloc(code->buffer_size);
            assert(images[i]);
            memcpy(images[i], code->buffer, code->buffer_size);
            const goat_module_container *container = validated_module_container(module);
            size_t n = goat_module_container_native_count(container);
            native_hashes[i] = calloc(n ? n : 1, 65);
            assert(native_hashes[i]);
            for (size_t j = 0; j < n; ++j) {
                goat_container_native native;
                assert(goat_module_container_native_at(container, j, &native));
                digest(native.bytes.data, native.bytes.size, native_hashes[i] + j * 65);
            }
            object_static_string_t storage = {0};
            object_t *x = get_static_string_object(&storage, U"x");
            module_files_t *files = module_snapshot_files(snapshot, i);
            for (size_t j = 0; j < module_files_count(files); ++j)
                assert(!get_own_field_cell(module_file_scope(module_files_get(files, (uint32_t)j)),
                                           x));
        }
        puts("READY");
        assert(!FFLUSH(stdout));
        if (!strcmp(argv[2], "sync"))
            assert(FGETC(stdin) == 'G');
        yyjson_mut_val *nodes = yyjson_mut_arr(doc);
        yyjson_mut_obj_add_val(doc, result, "nodes", nodes);
        for (size_t i = 0; i < count; ++i) {
            const module_graph_node_t *node = module_graph_node(graph, i);
            module_files_t *files = module_snapshot_files(snapshot, i);
            bytecode_t *code = module_files_bytecode(files);
            const bytecode_t *image = validated_module_bytecode(node->snapshot);
            yyjson_mut_val *row = yyjson_mut_obj(doc), *values = yyjson_mut_arr(doc);
            yyjson_mut_arr_append(nodes, row);
            yyjson_mut_obj_add_strcpy(doc, row, "name", node->name);
            yyjson_mut_obj_add_strcpy(doc, row, "version", node->version);
            yyjson_mut_obj_add_val(doc, row, "values", values);
            yyjson_mut_val *targets = yyjson_mut_arr(doc);
            yyjson_mut_obj_add_val(doc, row, "targets", targets);
            size_t dep_count = module_snapshot_dependency_count(snapshot, i);
            for (size_t j = 0; j < dep_count; ++j) {
                size_t target = module_snapshot_dependency(snapshot, i, j);
                const module_graph_node_t *dependency = module_graph_node(graph, target);
                assert(target < count);
                assert(module_snapshot_import(snapshot, i, dependency->name, dependency->version)
                       == target);
                module_files_t *target_files = module_snapshot_files(snapshot, target);
                assert(target_files
                       == module_snapshot_files(snapshot,
                                                module_snapshot_import(snapshot,
                                                                       i,
                                                                       dependency->name,
                                                                       dependency->version)));
                yyjson_mut_arr_add_uint(doc, targets, target);
            }
            assert(module_snapshot_dependency(snapshot, i, dep_count) == SIZE_MAX);
            /* Existing global nodes must not become implicit imports. */
            for (size_t target = 0; target < count; ++target) {
                const module_graph_node_t *candidate = module_graph_node(graph, target);
                size_t found =
                    module_snapshot_import(snapshot, i, candidate->name, candidate->version);
                bool direct = false;
                for (size_t j = 0; j < dep_count; ++j)
                    direct |= module_snapshot_dependency(snapshot, i, j) == target;
                assert(found == (direct ? target : SIZE_MAX));
            }
            size_t failures = 0;
            for (size_t j = 0; j < module_files_count(files); ++j) {
                module_file_t *file = module_files_get(files, (uint32_t)j);
                failures += execute(snapshot, files, file) != 0;
                object_static_string_t storage = {0}, string_storage = {0};
                object_t *x = get_static_string_object(&storage, U"x");
                field_cell_t *cell = get_own_field_cell(module_file_scope(file), x);
                assert(cell && cell->binding_unit == code->declaration_unit);
                int_value_t value = get_object_integer_value(cell->object);
                assert(value.has_value);
                yyjson_mut_arr_add_sint(doc, values, value.value);
                object_t *label = get_static_string_object(&string_storage, U"label");
                field_cell_t *string_cell = get_own_field_cell(module_file_scope(file), label);
                assert(string_cell);
                string_value_t text = convert_object_to_string(string_cell->object);
                char *utf8 = encode_utf8(text.data);
                assert(utf8);
                yyjson_mut_obj_add_strcpy(doc, row, "label", utf8);
                FREE(utf8);
                FREE_STRING(text);
            }
            yyjson_mut_obj_add_uint(doc, row, "failures", failures);
            assert(!memcmp(images[i], image->buffer, image->buffer_size));
            assert(!memcmp(images[i], code->buffer, code->buffer_size));
            assert(!image->declaration_unit && !image->native_functions);
            assert(code->declaration_unit); /* Mutable binding stays outside shared wire bytes. */
            const goat_module_container *container = validated_module_container(node->snapshot);
            yyjson_mut_val *native = yyjson_mut_arr(doc);
            yyjson_mut_obj_add_val(doc, row, "native", native);
            for (size_t j = 0; j < goat_module_container_native_count(container); ++j) {
                goat_container_native library;
                assert(goat_module_container_native_at(container, j, &library));
                char hash[65];
                digest(library.bytes.data, library.bytes.size, hash);
                assert(!strcmp(hash, native_hashes[i] + j * 65));
                yyjson_mut_arr_add_strcpy(doc, native, hash);
            }
            free(native_hashes[i]);
            free(images[i]);
        }
        free(native_hashes);
        free(images);
        collect_garbage(module_snapshot_process(snapshot));
        free_module_snapshot(snapshot);
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    assert(json);
    puts(json);
    free(json);
    yyjson_mut_doc_free(doc);
    assert(!get_allocated_memory_size());
    return 0;
}
