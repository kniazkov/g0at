/** @file module_compilation.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Resolve include expressions from frozen source graphs and link one module.
 */
#include "module_compilation.h"

#include "analysis/analysis.h"
#include "analysis/function_call_graph.h"
#include "analysis/function_effects.h"
#include "analysis/function_return.h"
#include "analysis/interpreter.h"
#include "analysis/module_interface.h"
#include "cli/options.h"
#include "codegen/linker.h"
#include "graph/declarations.h"
#include "graph/expression.h"
#include "graph/function_call.h"
#include "graph/module_import.h"
#include "graph/module_include.h"
#include "lib/allocate.h"
#include "lib/arena.h"
#include "lib/string_ext.h"
#include "lib/wire.h"
#include "module/module_hash.h"
#include "parser/parser.h"
#include "vm/module_bytecode.h"

#include <math.h>
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
    for (size_t i = 0; i < result->warning_count; ++i)
        FREE(result->warnings[i].message);
    FREE(result->warnings);
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

/** Do not lift observations from particular closure activations into diagnostics. */
static size_t count_call_warnings(const node_t *node) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        return 0;
    size_t count = module_call_is_noncallable(node) ? 1 : 0;
    for (size_t i = 0; i < get_node_child_count(node); ++i)
        count += count_call_warnings(get_node_child(node, i));
    return count;
}

static void collect_call_warnings(const node_t *node, size_t file, module_compilation_t *result) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT)
        return;
    if (module_call_is_noncallable(node)) {
        module_compilation_warning_t *warning = result->warnings + result->warning_count++;
        warning->file = file;
        if (node->position) {
            warning->row = node->position->begin->row;
            warning->column = node->position->begin->column;
        }
        const char message[] = "statically known value is not callable";
        warning->message = ALLOC(sizeof(message));
        memcpy(warning->message, message, sizeof(message));
    }
    for (size_t i = 0; i < get_node_child_count(node); ++i)
        collect_call_warnings(get_node_child(node, i), file, result);
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

typedef struct {
    const module_graph_t *graph;
    size_t owner;
} interface_context_t;

static yyjson_val *snapshot_metadata(const validated_module_t *snapshot) {
    const yyjson_doc *doc = goat_module_container_metadata(validated_module_container(snapshot));
    return doc ? yyjson_doc_get_root((yyjson_doc *)doc) : NULL;
}

/** Resolve only along the exporting module's own already-validated edges. */
static yyjson_val *resolve_interface(void *opaque, const char *name, const char *version) {
    const interface_context_t *context = opaque;
    size_t count = module_graph_node_count(context->graph);
    bool *seen = CALLOC(count * sizeof(*seen));
    size_t *queue = ALLOC(count * sizeof(*queue));
    size_t first = 0, end = 0;
    queue[end++] = context->owner;
    seen[context->owner] = true;
    yyjson_val *result = NULL;
    while (first < end && !result) {
        size_t current = queue[first++];
        for (size_t i = 0; i < module_graph_edge_count(context->graph); ++i) {
            const module_graph_edge_t *edge = module_graph_edge(context->graph, i);
            if (edge->from != current || seen[edge->to])
                continue;
            seen[edge->to] = true;
            queue[end++] = edge->to;
            const module_graph_node_t *target = module_graph_node(context->graph, edge->to);
            if (target->name && target->version && !strcmp(target->name, name)
                && !strcmp(target->version, version)) {
                result = snapshot_metadata(target->snapshot);
                break;
            }
        }
    }
    FREE(queue);
    FREE(seen);
    return result;
}

/** Source analysis may borrow a graph only when its root pins the same direct snapshots. */
static bool matching_graph(const module_dependencies_t *dependencies, const module_graph_t *graph) {
    if (!graph)
        return true;
    const module_graph_node_t *root = module_graph_node(graph, 0);
    if (!root || root->snapshot || module_graph_issue_count(graph))
        return false;
    for (size_t i = 1; i < module_graph_node_count(graph); ++i) {
        const module_graph_node_t *node = module_graph_node(graph, i);
        if (!node || !node->snapshot || !node->name || !node->version || !node->hash)
            return false;
    }
    size_t direct = 0;
    for (size_t i = 0; i < module_graph_edge_count(graph); ++i) {
        const module_graph_edge_t *edge = module_graph_edge(graph, i);
        if (edge->from)
            continue;
        ++direct;
        const module_graph_node_t *node = module_graph_node(graph, edge->to);
        bool matched = false;
        for (size_t j = 0; j < module_dependencies_count(dependencies); ++j) {
            yyjson_val *metadata = snapshot_metadata(module_dependency_snapshot(dependencies, j));
            matched |=
                node && node->snapshot && node->name && node->version && node->hash
                && !strcmp(node->name, yyjson_get_str(yyjson_obj_get(metadata, "name")))
                && !strcmp(node->version, yyjson_get_str(yyjson_obj_get(metadata, "version")))
                && !strcmp(node->hash, yyjson_get_str(yyjson_obj_get(metadata, "mainHash")));
        }
        if (!matched)
            return false;
    }
    return direct == module_dependencies_count(dependencies);
}

static void bind_interface(node_t *node,
                           const module_import_dependency_t *dependency,
                           const module_dependencies_t *dependencies,
                           const module_graph_t *graph,
                           arena_t *arena) {
    module_interface_t *interface = alloc_zeroed_from_arena(arena, sizeof(*interface));
    for (size_t i = 0; i < module_dependencies_count(dependencies); ++i) {
        yyjson_val *metadata = snapshot_metadata(module_dependency_snapshot(dependencies, i));
        if (!strcmp(dependency->name, yyjson_get_str(yyjson_obj_get(metadata, "name")))
            && !strcmp(dependency->version, yyjson_get_str(yyjson_obj_get(metadata, "version")))) {
            interface->metadata = metadata;
            break;
        }
    }
    if (graph) {
        for (size_t i = 1; i < module_graph_node_count(graph); ++i) {
            const module_graph_node_t *owner = module_graph_node(graph, i);
            if (!strcmp(owner->name, dependency->name)
                && !strcmp(owner->version, dependency->version)) {
                interface_context_t *context = alloc_from_arena(arena, sizeof(*context));
                *context = (interface_context_t){graph, i};
                interface->resolve = resolve_interface;
                interface->context = context;
                break;
            }
        }
    }
    set_module_import_interface(node, interface);
}

static bool bind_imports(size_t file,
                         const module_dependencies_t *dependencies,
                         const module_graph_t *graph,
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
            bind_interface(node, dep, dependencies, graph, memory->graph);
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

/** Lattice joins preserve signed zero but do not prove a particular NaN payload. */
static void write_value_facts(yyjson_mut_doc *doc,
                              yyjson_mut_val *out,
                              const lattice_element_t *value,
                              bool scalar,
                              bool exact_real) {
    if (!value)
        return;
    const char *type = NULL;
    switch (value->type) {
        case LATTICE_NULL:
            type = "null";
            break;
        case LATTICE_TRUE:
        case LATTICE_FALSE:
        case LATTICE_BOOLEAN:
            type = "boolean";
            break;
        case LATTICE_INTEGER:
        case LATTICE_INTEGER_RANGE:
        case LATTICE_INTEGER_CONSTANT:
            type = "integer";
            break;
        case LATTICE_REAL:
        case LATTICE_REAL_CONSTANT:
            type = "real";
            break;
        case LATTICE_NUMERIC:
            type = "numeric";
            break;
        case LATTICE_CHAR:
        case LATTICE_CHAR_CONSTANT:
            type = "character";
            break;
        case LATTICE_STRING:
        case LATTICE_STRING_CONSTANT:
            type = "string";
            break;
        case LATTICE_FUNCTION:
        case LATTICE_KNOWN_FUNCTION:
            type = "function";
            break;
        case LATTICE_ARRAY:
        case LATTICE_TYPED_ARRAY:
            type = "array";
            break;
        case LATTICE_USER_DEFINED_OBJECT:
            type = "object";
            break;
        case LATTICE_NOT_NULL:
            if (!scalar)
                yyjson_mut_obj_add_bool(doc, out, "nonNull", true);
            break;
        default:
            break;
    }
    if (type)
        yyjson_mut_obj_add_str(doc, out, "type", type);
    if (!scalar)
        return;
    switch (value->type) {
        case LATTICE_INTEGER_CONSTANT:
            yyjson_mut_obj_add_sint(doc,
                                    out,
                                    "value",
                                    ((const integer_constant_element_t *)value)->value);
            break;
        case LATTICE_TRUE:
        case LATTICE_FALSE:
            yyjson_mut_obj_add_bool(doc, out, "value", value->type == LATTICE_TRUE);
            break;
        case LATTICE_REAL_CONSTANT: {
            double real = ((const real_constant_element_t *)value)->value;
            if (!exact_real && isnan(real))
                break;
            uint64_t bits;
            memcpy(&bits, &real, sizeof(bits));
            char hex[17];
            static const char digits[] = "0123456789abcdef";
            for (size_t i = 0; i < 16; ++i)
                hex[i] = digits[(bits >> ((15 - i) * 4)) & 15];
            hex[16] = 0;
            yyjson_mut_obj_add_strcpy(doc, out, "value", hex);
            break;
        }
        case LATTICE_STRING_CONSTANT:
        case LATTICE_CHAR_CONSTANT: {
            uint32_t character = value->type == LATTICE_CHAR_CONSTANT
                                     ? ((const char_constant_element_t *)value)->value
                                     : 0;
            string_view_t text = value->type == LATTICE_STRING_CONSTANT
                                     ? ((const string_constant_element_t *)value)->value
                                     : (string_view_t){&character, 1};
            bool valid = true;
            for (size_t i = 0; i < text.length; ++i)
                valid &=
                    text.data[i] <= 0x10ffff && !(text.data[i] >= 0xd800 && text.data[i] <= 0xdfff);
            if (valid) {
                size_t length;
                char *utf8 = encode_utf8_view(text, &length);
                yyjson_mut_obj_add_strncpy(doc, out, "value", utf8, length);
                FREE(utf8);
            }
            break;
        }
        default:
            break;
    }
}

/** Retain only completed isolated proofs, never concrete-call/capture samples. */
static void write_analysis(yyjson_mut_doc *doc, yyjson_mut_val *record, const node_t *function) {
    const function_summary_set_t *set = get_function_summaries(function);
    yyjson_mut_val *analysis = yyjson_mut_obj(doc), *summaries = yyjson_mut_arr(doc);
    yyjson_mut_obj_add_uint(doc, analysis, "formatVersion", 1);
    yyjson_mut_obj_add_val(doc, analysis, "summaries", summaries);
    for (const function_summary_t *s = set->head; s; s = s->next) {
        if (s->status != FUNCTION_ANALYZED)
            continue;
        yyjson_mut_val *summary = yyjson_mut_obj(doc), *when = yyjson_mut_obj(doc);
        yyjson_mut_val *arguments = yyjson_mut_arr(doc);
        yyjson_mut_obj_add_val(doc, when, "arguments", arguments);
        yyjson_mut_obj_add_val(doc, summary, "when", when);
        for (size_t i = 0; i < s->parameter_count; ++i) {
            yyjson_mut_val *facts = yyjson_mut_obj(doc);
            write_value_facts(doc, facts, s->parameter_types[i], false, false);
            yyjson_mut_arr_append(arguments, facts);
        }
        if (s->return_type->type == LATTICE_BOTTOM)
            yyjson_mut_obj_add_bool(doc, summary, "normalReturn", false);
        else {
            yyjson_mut_val *result = yyjson_mut_obj(doc);
            write_value_facts(doc, result, s->return_type, false, false);
            yyjson_mut_obj_add_val(doc, summary, "result", result);
        }
        yyjson_mut_val *effects = yyjson_mut_obj(doc), *may = yyjson_mut_arr(doc);
        const char *names[] = {"input", "output", "externalRead", "externalWrite"};
        const uint32_t bits[] = {FUNCTION_EFFECT_INPUT,
                                 FUNCTION_EFFECT_OUTPUT,
                                 FUNCTION_EFFECT_EXTERNAL_READ,
                                 FUNCTION_EFFECT_EXTERNAL_WRITE};
        for (size_t i = 0; i < 4; ++i)
            if (s->effects & bits[i])
                yyjson_mut_arr_add_str(doc, may, names[i]);
        yyjson_mut_obj_add_val(doc, effects, "may", may);
        yyjson_mut_obj_add_bool(doc,
                                effects,
                                "unknown",
                                (s->effects & FUNCTION_EFFECT_UNKNOWN) != 0);
        yyjson_mut_obj_add_val(doc, summary, "effects", effects);
        yyjson_mut_arr_append(summaries, summary);
    }
    yyjson_mut_obj_add_val(doc, record, "analysis", analysis);
}

static const node_t *find_function(const node_t *node, uint32_t id) {
    if (node->vtbl->type == NODE_FUNCTION_OBJECT && node->bytecode_function_id == id)
        return node;
    for (size_t i = 0; i < get_node_child_count(node); ++i) {
        const node_t *found = find_function(get_node_child(node, i), id);
        if (found)
            return found;
    }
    return NULL;
}

/** Only the entry file's own top-level external declarations form the interface. */
static void write_externals(yyjson_mut_doc *doc,
                            yyjson_mut_val *out,
                            const node_t *node,
                            const scope_t *scope) {
    if (node->scope != scope)
        return;
    if (is_declarator(node->vtbl->type)) {
        const declarator_t *decl = (const declarator_t *)node;
        if (decl->modifier != ACCESS_EXTERNAL)
            return;
        yyjson_mut_val *record = yyjson_mut_obj(doc);
        bool mutable = node->vtbl->type != NODE_CONSTANT_DECLARATOR;
        yyjson_mut_obj_add_bool(doc, record, "mutable", mutable);
        if (!mutable) {
            const lattice_element_t *value = decl->abstract_value;
            const node_t *initial = get_node_child_count(node) ? get_node_child(node, 0) : NULL;
            bool exact_real = initial && initial->vtbl->type == NODE_REAL;
            if (exact_real)
                value = initial->vtbl->calculate((node_t *)initial, NULL, NULL);
            write_value_facts(doc, record, value, true, exact_real);
            if (value && value->type == LATTICE_KNOWN_FUNCTION) {
                const known_function_element_t *known = (const known_function_element_t *)value;
                if (known->imported) {
                    yyjson_mut_val *ref = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_str(doc, ref, "module", known->imported->module);
                    yyjson_mut_obj_add_str(doc, ref, "version", known->imported->version);
                    yyjson_mut_obj_add_uint(doc, ref, "functionId", known->imported->function_id);
                    yyjson_mut_obj_add_val(doc, record, "functionRef", ref);
                } else if (known->node && known->node->bytecode_function_id)
                    yyjson_mut_obj_add_uint(doc,
                                            record,
                                            "functionId",
                                            known->node->bytecode_function_id);
            }
        }
        size_t length;
        char *name = encode_utf8_view(decl->name, &length);
        yyjson_mut_obj_add(out, yyjson_mut_strncpy(doc, name, length), record);
        FREE(name);
        return;
    }
    for (size_t i = 0; i < get_node_child_count(node); ++i)
        write_externals(doc, out, get_node_child(node, i), scope);
}

static yyjson_doc *metadata(const module_sources_t *sources,
                            const bytecode_t *code,
                            const module_dependencies_t *dependencies,
                            node_t *const *roots) {
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
        const node_t *node = find_function(roots[f->file], f->id);
        if (node)
            write_analysis(doc, function, node);
        yyjson_mut_arr_append(functions, function);
    }
    yyjson_mut_val *externals = yyjson_mut_obj(doc);
    write_externals(doc, externals, roots[0], roots[0]->scope);
    yyjson_mut_obj_add_val(doc, root, "externals", externals);
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
    return compile_module_sources_with_graph(sources, dependencies, NULL, out, error);
}

bool compile_module_sources_with_graph(const module_sources_t *sources,
                                       const module_dependencies_t *dependencies,
                                       const module_graph_t *graph,
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
    if (!matching_dependencies(imports, dependencies) || !matching_graph(dependencies, graph)) {
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
    node_t **roots = CALLOC(sources->count * sizeof(*roots));
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
        if (!bind_imports(i,
                          dependencies,
                          graph,
                          bindings,
                          binding_count,
                          &binding_cursor,
                          memory,
                          &groups)) {
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
        roots[i] = root;
        interpret_module(root, memory, NULL);
        size_t warning_count = count_call_warnings(root);
        if (warning_count) {
            if (warning_count
                > SIZE_MAX / sizeof(module_compilation_warning_t) - result->warning_count) {
                fail(error, i, NULL, "module diagnostic size limit exceeded");
                goto done;
            }
            size_t total = result->warning_count + warning_count;
            module_compilation_warning_t *warnings = CALLOC(total * sizeof(*warnings));
            if (result->warning_count)
                memcpy(warnings, result->warnings, result->warning_count * sizeof(*warnings));
            FREE(result->warnings);
            result->warnings = warnings;
            collect_call_warnings(root, i, result);
        }
        function_call_graph_t *calls = build_function_call_graph(root, memory->graph, 1024);
        analyze_function_return_types(root);
        solve_function_recursion(calls, 64);
        analyze_function_direct_effects(root);
        analyze_function_c_expressions(root);
        propagate_function_effects(calls, 64);
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
    result->metadata = metadata(sources, result->code, dependencies, roots);
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
    FREE(roots);
    FREE(files);
    free_module_compilation(result);
    return ok;
}
