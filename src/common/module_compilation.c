/** @file module_compilation.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Resolve include expressions from frozen source graphs and link one module.
 */
#include "module_compilation.h"

#include "analysis/analysis.h"
#include "cli/options.h"
#include "codegen/linker.h"
#include "graph/module_import.h"
#include "graph/module_include.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "lib/wire.h"
#include "module/module_hash.h"
#include "parser/parser.h"
#include "vm/module_bytecode.h"

#include <stdlib.h>
#include <string.h>

void clear_module_compilation_error(module_compilation_error_t *error) {
    if (error) {
        FREE(error->message);
        *error = (module_compilation_error_t){0};
    }
}

void free_module_compilation(module_compilation_t *result) {
    if (!result)
        return;
    if (result->code)
        free_bytecode(result->code);
    yyjson_doc_free(result->metadata);
    FREE(result->entries);
    FREE(result);
}

static void fail(module_compilation_error_t *error,
                 size_t file,
                 compilation_error_t *diagnostic,
                 const char *fallback) {
    if (!error)
        return;
    error->file = file;
    if (diagnostic) {
        error->message = encode_utf8_view(diagnostic->message, NULL);
        if (diagnostic->position) {
            error->row = diagnostic->position->begin->row;
            error->column = diagnostic->position->begin->column;
        }
    } else {
        error->message = ALLOC(strlen(fallback) + 1);
        strcpy(error->message, fallback);
    }
}

/** Source occurrence offsets distinguish directives from member names and strings. */
static bool bind_includes(const module_source_t *source,
                          size_t count,
                          parser_memory_t *memory,
                          token_groups_t *groups) {
    size_t bound = 0;
    for (token_t *token = groups->identifiers.first; token && bound < source->includes.count;) {
        token_t *next = token->next_in_group;
        size_t offset = source->includes.items[bound].offset;
        if (token->position->begin->offset > offset)
            return false;
        if (token->position->begin->offset == offset) {
            token_t *literal = token->right;
            if (token->text.length != 7 || MEMCMP(token->text.data, U"include", 7) || !literal
                || !literal->node || literal->node->vtbl->type != NODE_STATIC_STRING
                || source->targets[bound] >= count)
                return false;
            node_t *node = create_module_include_node(memory->graph,
                                                      (uint32_t)source->targets[bound],
                                                      literal->node);
            collapse_tokens_to_token(memory, token, literal, TOKEN_EXPRESSION, node);
            ++bound;
        }
        token = next;
    }
    return bound == source->includes.count;
}

/** Flatten collected sites once, preserving exact pairs without reparsing defaults. */
typedef struct {
    module_import_site_t site;
    const module_import_dependency_t *dependency;
} import_binding_t;

static int compare_binding(const void *left, const void *right) {
    const import_binding_t *a = left, *b = right;
    if (a->site.file != b->site.file)
        return a->site.file < b->site.file ? -1 : 1;
    return a->site.offset < b->site.offset ? -1 : a->site.offset != b->site.offset;
}

static bool matching_dependencies(const module_imports_t *imports,
                                  const module_dependencies_t *dependencies) {
    if (imports->count != module_dependencies_count(dependencies))
        return false;
    const yyjson_doc *doc = module_dependencies_metadata(dependencies);
    yyjson_val *records = doc ? yyjson_doc_get_root((yyjson_doc *)doc) : NULL;
    if (imports->count && (!yyjson_is_arr(records) || yyjson_arr_size(records) != imports->count))
        return false;
    for (size_t i = 0; i < imports->count; ++i) {
        yyjson_val *record = yyjson_arr_get(records, i);
        const char *name = yyjson_get_str(yyjson_obj_get(record, "name"));
        const char *version = yyjson_get_str(yyjson_obj_get(record, "version"));
        if (!name || !version || strcmp(name, imports->dependencies[i].name)
            || strcmp(version, imports->dependencies[i].version))
            return false;
    }
    return true;
}

static bool bind_imports(size_t file,
                         const import_binding_t *bindings,
                         size_t count,
                         size_t *cursor,
                         parser_memory_t *memory,
                         token_groups_t *groups) {
    for (token_t *token = groups->identifiers.first;
         token && *cursor < count && bindings[*cursor].site.file == file;) {
        token_t *next = token->next_in_group;
        const import_binding_t *binding = bindings + *cursor;
        size_t offset = token->position->begin->offset;
        if (offset > binding->site.offset)
            return false;
        if (offset == binding->site.offset) {
            token_t *literal = token->right;
            if (token->text.length != 6 || MEMCMP(token->text.data, U"import", 6) || !literal
                || !literal->node || literal->node->vtbl->type != NODE_STATIC_STRING)
                return false;
            const module_import_dependency_t *dep = binding->dependency;
            size_t name_length = strlen(dep->name), version_length = strlen(dep->version);
            if (version_length > SIZE_MAX - 2 || name_length > SIZE_MAX - version_length - 2)
                return false;
            char *identity = ALLOC(name_length + version_length + 2);
            memcpy(identity, dep->name, name_length);
            identity[name_length] = '@';
            memcpy(identity + name_length + 1, dep->version, version_length + 1);
            string_value_t wide = decode_utf8(identity);
            FREE(identity);
            if (!wide.data)
                return false;
            node_t *node =
                create_module_import_node(memory->graph, VALUE_TO_VIEW(wide), literal->node);
            FREE_STRING(wide);
            collapse_tokens_to_token(memory, token, literal, TOKEN_EXPRESSION, node);
            ++*cursor;
        }
        token = next;
    }
    return *cursor == count || bindings[*cursor].site.file != file;
}

static yyjson_doc *normalized(yyjson_mut_doc *doc) {
    size_t length = 0;
    char *json = yyjson_mut_write(doc, 0, &length);
    yyjson_doc *out = NULL;
    if (json)
        goat_module_json_read(json, length, GOAT_MODULE_METADATA, &out);
    free(json);
    return out;
}

static yyjson_doc *metadata(const module_sources_t *sources,
                            const bytecode_t *code,
                            const module_dependencies_t *dependencies) {
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    yyjson_mut_obj_add_uint(doc, root, "formatVersion", 1);
    yyjson_mut_obj_add_uint(doc, root, "bytecodeVersion", 1);
    yyjson_mut_obj_add_str(doc, root, "name", "source");
    yyjson_mut_obj_add_str(doc, root, "version", "0.0.0");
    if (sources->descriptor) {
        const char *keys[] = {"name", "version", "description", "license", "homepage", "authors"};
        for (size_t i = 0; i < sizeof(keys) / sizeof(*keys); ++i) {
            yyjson_val *value = yyjson_obj_get(yyjson_doc_get_root(sources->descriptor), keys[i]);
            if (value)
                yyjson_mut_obj_put(root,
                                   yyjson_mut_str(doc, keys[i]),
                                   yyjson_val_mut_copy(doc, value));
        }
    }
    yyjson_mut_obj_add_str(doc,
                           root,
                           "mainHash",
                           "0000000000000000000000000000000000000000000000000000000000000000");
    yyjson_mut_obj_add_uint(doc, root, "main", 0);
    const yyjson_doc *dependency_metadata = module_dependencies_metadata(dependencies);
    if (dependency_metadata)
        yyjson_mut_obj_add_val(
            doc,
            root,
            "dependencies",
            yyjson_val_mut_copy(doc, yyjson_doc_get_root((yyjson_doc *)dependency_metadata)));
    yyjson_mut_val *files = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "files", files);
    for (size_t i = 0; i < sources->count; ++i) {
        yyjson_mut_val *file = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, file, "name", sources->files[i].name);
        yyjson_mut_obj_add_uint(doc, file, "entry", code->files[i].entry);
        yyjson_mut_obj_add_bool(doc, file, "external", sources->files[i].external);
        yyjson_mut_arr_append(files, file);
    }
    yyjson_mut_val *functions = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_val(doc, root, "functions", functions);
    for (size_t i = 0; i < code->function_count; ++i) {
        const bytecode_function_t *f = code->functions + i;
        yyjson_mut_val *function = yyjson_mut_obj(doc), *parameters = yyjson_mut_arr(doc);
        yyjson_mut_obj_add_uint(doc, function, "functionId", f->id);
        yyjson_mut_obj_add_uint(doc, function, "file", f->file);
        yyjson_mut_obj_add_uint(doc, function, "instruction", f->instruction);
        yyjson_mut_obj_add_val(doc, function, "parameters", parameters);
        for (size_t j = 0; j < f->parameter_count; ++j) {
            const data_descriptor_t *vector = code->data_descriptors + f->parameters;
            uint32_t index = read_le32(code->data + vector->offset + j * 4);
            const data_descriptor_t *name = code->data_descriptors + index;
            size_t length;
            char *utf8 = encode_utf8_view(
                (string_view_t){(const gchar_t *)(code->data + name->offset), name->count},
                &length);
            yyjson_mut_val *parameter = yyjson_mut_obj(doc);
            yyjson_mut_obj_add_strncpy(doc, parameter, "name", utf8, length);
            FREE(utf8);
            yyjson_mut_arr_append(parameters, parameter);
        }
        yyjson_mut_arr_append(functions, function);
    }
    yyjson_doc *out = normalized(doc);
    char hash[GOAT_MODULE_HASH_HEX_SIZE];
    if (out && !goat_module_main_hash(out, code->buffer, code->buffer_size, hash)) {
        yyjson_doc_free(out);
        yyjson_mut_obj_put(root, yyjson_mut_str(doc, "mainHash"), yyjson_mut_str(doc, hash));
        out = normalized(doc);
    } else {
        yyjson_doc_free(out);
        out = NULL;
    }
    yyjson_mut_doc_free(doc);
    return out;
}

bool compile_module_sources(const module_sources_t *sources,
                            module_compilation_t **out,
                            module_compilation_error_t *error) {
    return compile_module_sources_with_dependencies(sources, NULL, out, error);
}

bool compile_module_sources_with_dependencies(const module_sources_t *sources,
                                              const module_dependencies_t *dependencies,
                                              module_compilation_t **out,
                                              module_compilation_error_t *error) {
    if (out)
        *out = NULL;
    if (error)
        *error = (module_compilation_error_t){0};
    if (!out || !sources || !sources->files || !sources->count || sources->count > UINT32_MAX
        || sources->count > SIZE_MAX / sizeof(module_source_t)
        || sources->count > SIZE_MAX / sizeof(parser_memory_t)
        || sources->count > SIZE_MAX / sizeof(bytecode_file_t)
        || sources->count > SIZE_MAX / sizeof(uint32_t)) {
        fail(error, 0, NULL, "invalid source graph");
        return false;
    }
    for (size_t i = 0; i < sources->count; ++i) {
        const module_source_t *source = sources->files + i;
        if (!source->source || !source->name || !source->name[0]
            || source->length > SIZE_MAX / sizeof(gchar_t) - 1
            || source->includes.count > SIZE_MAX / sizeof(source_include_t)
            || source->includes.count > SIZE_MAX / sizeof(size_t)
            || (source->includes.count && (!source->includes.items || !source->targets))) {
            fail(error, i, NULL, "invalid source snapshot");
            return false;
        }
    }
    module_imports_t *imports = NULL;
    module_imports_error_t import_error = {0};
    if (module_imports_collect(sources, 0, &imports, &import_error)) {
        fail(error, import_error.site.file, NULL, import_error.message);
        if (error) {
            error->row = import_error.site.row;
            error->column = import_error.site.column;
        }
        return false;
    }
    if (!matching_dependencies(imports, dependencies)) {
        fail(error, 0, NULL, "source imports require exactly matching direct dependency bindings");
        free_module_imports(imports);
        return false;
    }
    size_t binding_count = 0, binding_cursor = 0;
    for (size_t i = 0; i < imports->count; ++i) {
        if (imports->dependencies[i].count > SIZE_MAX / sizeof(import_binding_t) - binding_count) {
            fail(error, 0, NULL, "too many import bindings");
            free_module_imports(imports);
            return false;
        }
        binding_count += imports->dependencies[i].count;
    }
    import_binding_t *bindings = binding_count ? ALLOC(binding_count * sizeof(*bindings)) : NULL;
    size_t position = 0;
    for (size_t i = 0; i < imports->count; ++i)
        for (size_t j = 0; j < imports->dependencies[i].count; ++j)
            bindings[position++] =
                (import_binding_t){imports->dependencies[i].sites[j], imports->dependencies + i};
    if (binding_count)
        qsort(bindings, binding_count, sizeof(*bindings), compare_binding);
    module_compilation_t *result = CALLOC(sizeof(*result));
    result->count = sources->count;
    result->entries = CALLOC(sources->count * sizeof(*result->entries));
    parser_memory_t *memories = CALLOC(sources->count * sizeof(*memories));
    bytecode_file_t *files = CALLOC(sources->count * sizeof(*files));
    code_builder_t *cb = create_code_builder();
    data_builder_t *db = create_data_builder();
    bool ok = false;
    for (size_t i = 0; i < sources->count; ++i) {
        const module_source_t *source = sources->files + i;
        parser_memory_t *memory = memories + i;
        *memory = (parser_memory_t){create_arena(64),
                                    create_arena(64),
                                    create_arena(128),
                                    create_arena(8)};
        string_value_t text = decode_utf8_n(source->source, source->length);
        if (!text.data) {
            fail(error, i, NULL, "source is not valid UTF-8");
            goto done;
        }
        token_groups_t groups = {0};
        scanner_t *scan = create_scanner(source->name, text, memory, &groups);
        FREE_STRING(text);
        token_list_t tokens;
        compilation_error_t *diagnostic = process_brackets(memory, scan, &tokens, &groups);
        if (diagnostic) {
            fail(error, i, diagnostic, NULL);
            goto done;
        }
        if (!bind_includes(source, sources->count, memory, &groups)) {
            fail(error, i, NULL, "source include graph does not match its snapshot");
            goto done;
        }
        if (!bind_imports(i, bindings, binding_count, &binding_cursor, memory, &groups)) {
            fail(error, i, NULL, "source import sites do not match their snapshot");
            goto done;
        }
        parsing_result_t parsed = {0};
        diagnostic = apply_reduction_rules(&groups, memory, &parsed);
        node_t *root = NULL;
        if (!diagnostic)
            diagnostic = process_root_token_list(memory, &tokens, &root);
        if (diagnostic) {
            fail(error, i, diagnostic, NULL);
            goto done;
        }
        options_t options = {.optimization_level = OPTIMIZATION_NONE};
        diagnostic = analyze(root, memory, &options, NULL);
        if (get_most_severe_compilation_error(diagnostic) > WARNING) {
            fail(error, i, diagnostic, NULL);
            goto done;
        }
        if (cb->size > UINT32_MAX) {
            fail(error, i, NULL, "module instruction limit exceeded");
            goto done;
        }
        cb->location = (bytecode_location_t){.file = (uint32_t)i};
        db->file = (uint32_t)i;
        files[i].first = files[i].entry = result->entries[i] = (uint32_t)cb->size;
        generate_bytecode_from_node(root, cb, db);
        bool pending, progress;
        do {
            pending = progress = false;
            for (list_item_t *item = parsed.functions->head; item;) {
                list_item_t *next = item->next;
                if (generate_deferred_bytecode_from_node(item->value.ptr, cb, db)) {
                    remove_item_from_linked_list(parsed.functions, item);
                    progress = true;
                } else
                    pending = true;
                item = next;
            }
        } while (pending && progress);
        if (pending) {
            fail(error, i, NULL, "unresolved deferred function");
            goto done;
        }
        if (cb->size > UINT32_MAX) {
            fail(error, i, NULL, "module instruction limit exceeded");
            goto done;
        }
        files[i].count = (uint32_t)cb->size - files[i].first;
    }
    result->code = link_module_code_and_data(cb, db, files, sources->count);
    if (!result->code) {
        fail(error, 0, NULL, "module image size limit exceeded");
        goto done;
    }
    result->metadata = metadata(sources, result->code, dependencies);
    if (!result->metadata) {
        fail(error, 0, NULL, "invalid generated module metadata");
        goto done;
    }
    size_t length = 0;
    char *json = yyjson_write(result->metadata, 0, &length);
    bytecode_t *checked = NULL;
    uint32_t entry;
    module_bytecode_error_t check;
    module_bytecode_status_t status = read_module_bytecode(json,
                                                           length,
                                                           result->code->buffer,
                                                           result->code->buffer_size,
                                                           &checked,
                                                           &entry,
                                                           &check);
    free(json);
    if (checked)
        free_bytecode(checked);
    if (status) {
        fail(error, check.index, NULL, module_bytecode_status_string(status));
        goto done;
    }
    *out = result;
    result = NULL;
    ok = true;
done:
    FREE(bindings);
    free_module_imports(imports);
    destroy_code_builder(cb);
    destroy_data_builder(db);
    for (size_t i = 0; i < sources->count; ++i) {
        if (!memories[i].positions)
            continue;
        destroy_arena(memories[i].positions);
        destroy_arena(memories[i].tokens);
        destroy_arena(memories[i].graph);
        destroy_arena(memories[i].errors);
    }
    FREE(memories);
    FREE(files);
    free_module_compilation(result);
    return ok;
}
