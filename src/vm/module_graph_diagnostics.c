/** @file module_graph_diagnostics.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Complete path expansion and stable dependency diagnostic presentation.
 */
#include "module_graph_diagnostics.h"

#include "lib/allocate.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    module_graph_diagnostic_t view;
    const module_graph_t *graph;
} row_t;

typedef struct {
    const module_graph_t *graph;
    row_t *rows;
    size_t count, capacity;
    module_graph_diagnostic_t current;
} collection_t;

static void *array(size_t count, size_t size) {
    if (count > SIZE_MAX / size)
        abort();
    return ALLOC(count * size);
}

static bool collect(const size_t *path, size_t count, void *context) {
    collection_t *c = context;
    if (c->current.kind == MODULE_GRAPH_CYCLE) {
        /* An alternate approach to a DFS back edge is not itself a cycle.
         * Only label closed witnesses, with a repeated terminal identity. */
        bool closed = c->current.node == 0;
        for (size_t i = 0; i + 1 < count && !closed; i++)
            closed = module_graph_edge(c->graph, path[i])->to == c->current.node;
        if (!closed)
            return true;
    }
    if (c->count == c->capacity) {
        if (c->capacity > SIZE_MAX / 2)
            abort();
        size_t next = c->capacity ? c->capacity * 2 : 16;
        row_t *rows = array(next, sizeof(*rows));
        if (c->count)
            memcpy(rows, c->rows, c->count * sizeof(*rows));
        FREE(c->rows);
        c->rows = rows;
        c->capacity = next;
    }
    size_t *copy = array(count, sizeof(*copy));
    if (count)
        memcpy(copy, path, count * sizeof(*copy));
    row_t *row = &c->rows[c->count++];
    *row = (row_t){c->current, c->graph};
    row->view.path = copy;
    row->view.path_count = count;
    row->view.edge = count ? path[count - 1] : SIZE_MAX;
    return true;
}

static int number(size_t a, size_t b) {
    return (a > b) - (a < b);
}

static int text(const char *a, const char *b) {
    return strcmp(a ? a : "", b ? b : "");
}

static int bytes(const void *a, size_t an, const void *b, size_t bn) {
    size_t n = an < bn ? an : bn;
    int order = n ? memcmp(a, b, n) : 0;
    return order ? order : number(an, bn);
}

static int compare(const void *left, const void *right) {
    const row_t *a = left, *b = right;
    const module_graph_diagnostic_t *x = &a->view, *y = &b->view;
    size_t n = x->path_count < y->path_count ? x->path_count : y->path_count;
    for (size_t i = 0; i < n; i++) {
        const module_graph_node_t *xn =
            module_graph_node(a->graph, module_graph_edge(a->graph, x->path[i])->to);
        const module_graph_node_t *yn =
            module_graph_node(b->graph, module_graph_edge(b->graph, y->path[i])->to);
        int order = text(xn->name, yn->name);
        if (!order)
            order = text(xn->version, yn->version);
        if (order)
            return order;
    }
    int order = number(x->path_count, y->path_count);
    if (!order)
        order = number(x->kind, y->kind);
    const module_validation_diagnostic_t *xd = x->validation, *yd = y->validation;
    if (!order)
        order = number(xd != NULL, yd != NULL);
    if (order)
        return order;
    if (xd) {
#define CMP(field)                                                                                 \
    do {                                                                                           \
        order = number(xd->field, yd->field);                                                      \
        if (order)                                                                                 \
            return order;                                                                          \
    } while (0)
        CMP(check);
        CMP(detail);
        CMP(state);
        CMP(container.status);
        CMP(container.zip_error);
        CMP(container.json_error);
        CMP(bytecode.status);
        CMP(bytecode.json_error);
        CMP(native.status);
        order = bytes(xd->entry.data, xd->entry.size, yd->entry.data, yd->entry.size);
        if (order)
            return order;
        order = bytes(xd->field, xd->field_length, yd->field, yd->field_length);
        if (order)
            return order;
        CMP(index);
        CMP(bytecode.index);
        CMP(native.variant_index);
        CMP(prerequisite);
        CMP(container_prerequisite);
        CMP(system_error);
#undef CMP
    }
    order = bytes(x->reference, x->reference_length, y->reference, y->reference_length);
    if (order)
        return order;
    for (size_t i = 0; i < n; i++) {
        order = text(module_graph_edge(a->graph, x->path[i])->hash,
                     module_graph_edge(b->graph, y->path[i])->hash);
        if (order)
            return order;
    }
    return 0;
}

/* Detect cyclic edges before path expansion: unrelated layered DAGs must not
 * generate exponential discarded paths merely because another branch cycles.
 * This work is iterative and O(E * (V + E)), only on explicit error reporting. */
static void cycles(collection_t *c) {
    size_t nodes = module_graph_node_count(c->graph), edges = module_graph_edge_count(c->graph);
    size_t *first = array(nodes, sizeof(*first)), *next = array(edges, sizeof(*next));
    size_t *queue = array(nodes, sizeof(*queue));
    bool *seen = array(nodes, sizeof(*seen));
    for (size_t i = 0; i < nodes; i++)
        first[i] = SIZE_MAX;
    for (size_t i = 0; i < edges; i++) {
        size_t from = module_graph_edge(c->graph, i)->from;
        next[i] = first[from];
        first[from] = i;
    }
    for (size_t i = 0; i < edges; i++) {
        const module_graph_edge_t *edge = module_graph_edge(c->graph, i);
        memset(seen, 0, nodes * sizeof(*seen));
        size_t head = 0, tail = 1;
        queue[0] = edge->to;
        seen[edge->to] = true;
        while (head < tail && !seen[edge->from]) {
            for (size_t e = first[queue[head++]]; e != SIZE_MAX; e = next[e]) {
                size_t to = module_graph_edge(c->graph, e)->to;
                if (!seen[to]) {
                    seen[to] = true;
                    queue[tail++] = to;
                }
            }
        }
        if (seen[edge->from]) {
            c->current.node = edge->to;
            module_graph_paths(c->graph, edge->to, i, collect, c);
        }
    }
    FREE(seen);
    FREE(queue);
    FREE(next);
    FREE(first);
}

bool report_module_graph_diagnostics(const module_graph_t *graph,
                                     module_graph_diagnostic_report_t report,
                                     void *context) {
    if (!graph || !report)
        return false;
    collection_t c = {.graph = graph};
    size_t nodes = module_graph_node_count(graph);
    bool *conflicts = array(nodes, sizeof(*conflicts));
    memset(conflicts, 0, nodes * sizeof(*conflicts));
    bool cycles_reported = false;
    for (size_t i = 0; i < module_graph_issue_count(graph); i++) {
        const module_graph_issue_t *issue = module_graph_issue(graph, i);
        c.current = (module_graph_diagnostic_t){.kind = issue->kind,
                                                .node = issue->node,
                                                .reference = issue->reference,
                                                .reference_length = issue->reference_length};
        if (issue->kind == MODULE_GRAPH_CYCLE) {
            if (!cycles_reported)
                cycles(&c);
            cycles_reported = true;
        } else if (issue->kind == MODULE_GRAPH_CONFLICT) {
            if (conflicts[issue->node])
                continue;
            conflicts[issue->node] = true;
            for (size_t e = 0; e < module_graph_edge_count(graph); e++)
                if (module_graph_edge(graph, e)->to == issue->node)
                    module_graph_paths(graph, issue->node, e, collect, &c);
        } else {
            module_graph_paths(graph, issue->node, issue->edge, collect, &c);
            if (issue->kind == MODULE_GRAPH_SEARCH)
                for (size_t j = 0; j < module_graph_validation_count(graph, issue->node); j++) {
                    c.current.validation = module_graph_validation(graph, issue->node, j);
                    module_graph_paths(graph, issue->node, SIZE_MAX, collect, &c);
                }
        }
    }
    FREE(conflicts);
    if (c.count > 1)
        qsort(c.rows, c.count, sizeof(*c.rows), compare);
    bool complete = true;
    for (size_t i = 0; i < c.count && complete; i++)
        complete = report(&c.rows[i].view, context);
    for (size_t i = 0; i < c.count; i++)
        FREE((void *)c.rows[i].view.path);
    FREE(c.rows);
    return complete;
}

typedef struct {
    char *data;
    size_t length, capacity;
    const module_graph_t *graph;
} output_t;

static void append(output_t *o, const char *format, ...) {
    va_list args, measure;
    va_start(args, format);
    va_copy(measure, args);
    int needed = vsnprintf(NULL, 0, format, measure);
    va_end(measure);
    if (needed < 0 || (size_t)needed >= SIZE_MAX - o->length)
        abort();
    size_t size = o->length + (size_t)needed + 1;
    if (size > o->capacity) {
        size_t cap = o->capacity ? o->capacity : 256;
        while (cap < size) {
            if (cap > SIZE_MAX / 2) {
                cap = size;
                break;
            }
            cap *= 2;
        }
        char *next = ALLOC(cap);
        if (o->length)
            memcpy(next, o->data, o->length);
        FREE(o->data);
        o->data = next;
        o->capacity = cap;
    }
    vsnprintf(o->data + o->length, (size_t)needed + 1, format, args);
    va_end(args);
    o->length += (size_t)needed;
}

/* Return the size of a well-formed UTF-8 scalar, never accept overlongs,
 * surrogates or values outside Unicode. ZIP names need not be valid UTF-8. */
static size_t scalar(const unsigned char *p, size_t left) {
    size_t n = p[0] >= 0xc2 && p[0] <= 0xdf   ? 2
               : p[0] >= 0xe0 && p[0] <= 0xef ? 3
               : p[0] >= 0xf0 && p[0] <= 0xf4 ? 4
                                              : 0;
    if (!n || left < n)
        return 0;
    for (size_t i = 1; i < n; i++)
        if (p[i] < 0x80 || p[i] > 0xbf)
            return 0;
    if ((p[0] == 0xe0 && p[1] < 0xa0) || (p[0] == 0xed && p[1] >= 0xa0)
        || (p[0] == 0xf0 && p[1] < 0x90) || (p[0] == 0xf4 && p[1] >= 0x90))
        return 0;
    return n;
}

static void quote(output_t *o, const void *data, size_t size) {
    const unsigned char *p = data;
    append(o, "\"");
    for (size_t i = 0; i < size; i++) {
        if (p[i] >= 128) {
            size_t n = scalar(p + i, size - i);
            if (n) {
                unsigned value = p[i] & (n == 2 ? 31 : n == 3 ? 15 : 7);
                for (size_t j = 1; j < n; j++)
                    value = (value << 6) | (p[i + j] & 63);
                if (value <= 0x9f || (value >= 0x2028 && value <= 0x202e)
                    || (value >= 0x2066 && value <= 0x2069))
                    append(o, "\\u%04x", value);
                else
                    append(o, "%.*s", (int)n, (const char *)p + i);
                i += n - 1;
            } else
                append(o, "\\x%02x", p[i]);
        } else if (p[i] < 32 || p[i] == 127)
            append(o, "\\x%02x", p[i]);
        else if (p[i] == '\\' || p[i] == '"')
            append(o, "\\%c", p[i]);
        else
            append(o, "%c", p[i]);
    }
    append(o, "\"");
}

static void label(output_t *o, const char *s) {
    quote(o, s ? s : "", s ? strlen(s) : 0);
}

static void identity(output_t *o, size_t index) {
    const module_graph_node_t *node = module_graph_node(o->graph, index);
    if (node->name)
        append(o, "%s@%s", node->name, node->version);
    else {
        append(o, "root ");
        label(o, node->archive);
    }
}

static bool render(const module_graph_diagnostic_t *d, void *context) {
    output_t *o = context;
    identity(o, 0);
    for (size_t i = 0; i < d->path_count; i++) {
        append(o, " -> ");
        identity(o, module_graph_edge(o->graph, d->path[i])->to);
    }
    const module_graph_node_t *node = module_graph_node(o->graph, d->node);
    const module_graph_edge_t *edge = module_graph_edge(o->graph, d->edge);
    append(o, ": ");
    if (d->validation) {
        const module_validation_diagnostic_t *v = d->validation;
        static const char *checks[] = {"input", "container", "bytecode", "native"};
        append(o,
               "validation.%s %s (detail %d)",
               checks[v->check],
               v->state == MODULE_CHECK_SKIPPED ? "skipped" : "failed",
               v->detail);
        if (v->state == MODULE_CHECK_SKIPPED)
            append(o, "; prerequisite %s/%d", checks[v->prerequisite], v->container_prerequisite);
        else if (v->check == MODULE_CHECK_CONTAINER)
            append(o,
                   "; %s (ZIP %d, JSON %d)",
                   goat_container_status_string(v->container.status),
                   v->container.zip_error,
                   v->container.json_error);
        else if (v->check == MODULE_CHECK_BYTECODE)
            append(o,
                   "; %s (JSON %d)",
                   module_bytecode_status_string(v->bytecode.status),
                   v->bytecode.json_error);
        else if (v->check == MODULE_CHECK_NATIVE)
            append(o, "; %s", goat_native_status_string(v->native.status));
        if (v->entry.size) {
            append(o, "; entry ");
            quote(o, v->entry.data, v->entry.size);
        }
        if (v->field_length) {
            append(o, "; field ");
            quote(o, v->field, v->field_length);
        }
        if (v->index != SIZE_MAX)
            append(o, "; index %zu", v->index);
        if (v->system_error)
            append(o, "; system error %d", v->system_error);
    } else
        switch (d->kind) {
            case MODULE_GRAPH_SEARCH: {
                static const char *causes[] = {"success",
                                               "invalid search arguments",
                                               "filesystem failure",
                                               "module not found",
                                               "invalid or corrupt module",
                                               "module name/version mismatch",
                                               "mainHash mismatch"};
                const module_search_error_t *e = &node->search;
                append(o, "search: %s", causes[e->status]);
                if (e->filesystem.status) {
                    static const char *causes[] = {"success",
                                                   "invalid path or object type",
                                                   "path disappeared or link target missing",
                                                   "access denied",
                                                   "already exists",
                                                   "unsupported filesystem operation",
                                                   "filesystem object changed",
                                                   "I/O failure"};
                    append(o,
                           "; %s (category %d), native code %lu, operation ",
                           causes[e->filesystem.status],
                           e->filesystem.status,
                           e->filesystem.native_code);
                    label(o, e->filesystem.operation);
                    append(o, "; path ");
                    label(o, e->filesystem.path);
                }
                append(o, "; checked paths:");
                for (size_t i = 0; i < e->count; i++) {
                    append(o, " ");
                    label(o, e->attempted[i]);
                }
                if (e->count && e->status != MODULE_SEARCH_NOT_FOUND) {
                    append(o, "; stopped at ");
                    label(o, e->attempted[e->count - 1]);
                }
                break;
            }
            case MODULE_GRAPH_HASH:
            case MODULE_GRAPH_CONFLICT:
                append(o,
                       "%s; expected SHA-256 %s; actual SHA-256 %s",
                       d->kind == MODULE_GRAPH_HASH ? "mainHash mismatch"
                                                    : "conflicting mainHash requests",
                       edge && edge->hash ? edge->hash : "unavailable",
                       node->hash ? node->hash : "unavailable");
                break;
            case MODULE_GRAPH_CYCLE:
                append(o, "import cycle");
                break;
            case MODULE_GRAPH_REFERENCE:
                append(o, "invalid functionRef; export ");
                quote(o, d->reference, d->reference_length);
                break;
        }
    append(o, "\n");
    return true;
}

char *format_module_graph_diagnostics(const module_graph_t *graph) {
    if (!graph)
        return NULL;
    output_t o = {.graph = graph};
    append(&o, "");
    report_module_graph_diagnostics(graph, render, &o);
    return o.data;
}
