/** @file module_information_merge.c
 * @brief Validated all-input information conflicts and explicit author ordering.
 */
#include "module_information_merge.h"

#include "lib/file_system.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct goat_information_merge {
    yyjson_doc *information;
    bool needs_order;
};

static int text_compare(const yyjson_val *a, const yyjson_val *b) {
    if (!a || !b)
        return (a != NULL) - (b != NULL);
    size_t na = yyjson_get_len(a), nb = yyjson_get_len(b);
    int cmp = memcmp(yyjson_get_str(a), yyjson_get_str(b), na < nb ? na : nb);
    return cmp ? cmp : (na > nb) - (na < nb);
}

static int record_compare(const yyjson_val *a, const yyjson_val *b, bool producer) {
    const char *keys[] = {"name", producer ? "version" : "email", "buildId"};
    for (size_t i = 0; i < (producer ? 3u : 2u); i++) {
        int cmp = text_compare(yyjson_obj_get(a, keys[i]), yyjson_obj_get(b, keys[i]));
        if (cmp)
            return cmp;
    }
    return 0;
}

static int producer_compare(const void *a, const void *b) {
    return record_compare(*(const yyjson_val *const *)a, *(const yyjson_val *const *)b, true);
}

static bool
append_unique(const yyjson_val ***items, size_t *count, const yyjson_val *value, bool producer) {
    for (size_t i = 0; i < *count; i++)
        if (!record_compare((*items)[i], value, producer))
            return true;
    if (*count >= SIZE_MAX / sizeof(**items))
        return false;
    const yyjson_val **next = realloc((void *)*items, (*count + 1) * sizeof(**items));
    if (!next)
        return false;
    next[(*count)++] = value;
    *items = next;
    return true;
}

static bool add_array(yyjson_mut_doc *doc,
                      yyjson_mut_val *root,
                      const char *key,
                      const yyjson_val *const *items,
                      size_t count) {
    if (!count)
        return true;
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    if (!array || !yyjson_mut_obj_add_val(doc, root, key, array))
        return false;
    for (size_t i = 0; i < count; i++) {
        yyjson_mut_val *copy = yyjson_val_mut_copy(doc, items[i]);
        if (!copy || !yyjson_mut_arr_append(array, copy))
            return false;
    }
    return true;
}

goat_information_merge_error
goat_information_merge_prepare(const yyjson_doc *const *inputs,
                               size_t count,
                               goat_information_merge **out,
                               goat_information_merge_diagnostic *diagnostic) {
    if (out)
        *out = NULL;
    if (diagnostic)
        *diagnostic = (goat_information_merge_diagnostic){SIZE_MAX, GOAT_MODULE_JSON_OK, NULL};
    if (!inputs || !count || !out)
        return GOAT_INFORMATION_MERGE_INVALID_INPUT;
    if (count > SIZE_MAX / sizeof(yyjson_doc *))
        return GOAT_INFORMATION_MERGE_MEMORY;
    yyjson_doc **normalized = calloc(count, sizeof(*normalized));
    if (!normalized)
        return GOAT_INFORMATION_MERGE_MEMORY;
    goat_information_merge_error error = GOAT_INFORMATION_MERGE_OK;
    const yyjson_val **authors = NULL, **producers = NULL;
    size_t author_count = 0, producer_count = 0, nonempty = 0;
    yyjson_mut_doc *result = NULL;
    goat_information_merge *plan = NULL;
    for (size_t i = 0; i < count; i++) {
        goat_module_json_error json_error =
            goat_module_information(inputs[i], GOAT_MODULE_METADATA, &normalized[i]);
        if (json_error) {
            if (diagnostic) {
                diagnostic->source_index = i;
                diagnostic->json_error = json_error;
            }
            error = json_error == GOAT_MODULE_JSON_MEMORY ? GOAT_INFORMATION_MERGE_MEMORY
                                                          : GOAT_INFORMATION_MERGE_INVALID_INPUT;
            goto done;
        }
    }
    result = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = result ? yyjson_mut_obj(result) : NULL;
    if (!root) {
        error = GOAT_INFORMATION_MERGE_MEMORY;
        goto done;
    }
    yyjson_mut_doc_set_root(result, root);
    const char *fields[] = {"description", "license", "homepage"};
    for (size_t f = 0; f < 3; f++) {
        yyjson_val *first = yyjson_obj_get(yyjson_doc_get_root(normalized[0]), fields[f]);
        for (size_t i = 1; i < count; i++) {
            yyjson_val *current = yyjson_obj_get(yyjson_doc_get_root(normalized[i]), fields[f]);
            if (text_compare(first, current)) {
                if (diagnostic) {
                    diagnostic->source_index = i;
                    diagnostic->field = fields[f];
                }
                error = GOAT_INFORMATION_MERGE_CONFLICT;
                goto done;
            }
        }
        if (first) {
            yyjson_mut_val *copy = yyjson_val_mut_copy(result, first);
            if (!copy || !yyjson_mut_obj_add_val(result, root, fields[f], copy)) {
                error = GOAT_INFORMATION_MERGE_MEMORY;
                goto done;
            }
        }
    }
    for (size_t i = 0; i < count; i++) {
        yyjson_val *input_root = yyjson_doc_get_root(normalized[i]);
        yyjson_val *array = yyjson_obj_get(input_root, "authors");
        if (yyjson_arr_size(array))
            nonempty++;
        size_t index, max;
        yyjson_val *value;
        yyjson_arr_foreach(array, index, max, value) {
            if (!append_unique(&authors, &author_count, value, false)) {
                error = GOAT_INFORMATION_MERGE_MEMORY;
                goto done;
            }
        }
        array = yyjson_obj_get(input_root, "producers");
        yyjson_arr_foreach(array, index, max, value) {
            if (!append_unique(&producers, &producer_count, value, true)) {
                error = GOAT_INFORMATION_MERGE_MEMORY;
                goto done;
            }
        }
    }
    if (producer_count > 1)
        qsort(producers, producer_count, sizeof(*producers), producer_compare);
    if (!add_array(result, root, "authors", authors, author_count)
        || !add_array(result, root, "producers", producers, producer_count)) {
        error = GOAT_INFORMATION_MERGE_MEMORY;
        goto done;
    }
    plan = calloc(1, sizeof(*plan));
    if (!plan || !(plan->information = yyjson_mut_doc_imut_copy(result, NULL))) {
        error = GOAT_INFORMATION_MERGE_MEMORY;
        goto done;
    }
    plan->needs_order = nonempty > 1;
    *out = plan;
    plan = NULL;
done:
    goat_information_merge_free(plan);
    yyjson_mut_doc_free(result);
    free(authors);
    free(producers);
    for (size_t i = 0; i < count; i++)
        yyjson_doc_free(normalized[i]);
    free(normalized);
    return error;
}

void goat_information_merge_free(goat_information_merge *plan) {
    if (plan) {
        yyjson_doc_free(plan->information);
        free(plan);
    }
}

const yyjson_val *goat_information_merge_candidates(const goat_information_merge *plan) {
    return plan ? yyjson_obj_get(yyjson_doc_get_root(plan->information), "authors") : NULL;
}

bool goat_information_merge_needs_order(const goat_information_merge *plan) {
    return plan && plan->needs_order;
}

goat_information_merge_error goat_information_merge_finish(const goat_information_merge *plan,
                                                           const size_t *order,
                                                           size_t count,
                                                           yyjson_doc **out) {
    if (out)
        *out = NULL;
    if (!plan || !out)
        return GOAT_INFORMATION_MERGE_INVALID_INPUT;
    const yyjson_val *authors = goat_information_merge_candidates(plan);
    size_t total = yyjson_arr_size(authors);
    if (plan->needs_order) {
        if (!order)
            return GOAT_INFORMATION_MERGE_ORDER_REQUIRED;
        if (count != total)
            return GOAT_INFORMATION_MERGE_ORDER_MISSING;
        for (size_t i = 0; i < count; i++) {
            if (order[i] >= total)
                return GOAT_INFORMATION_MERGE_ORDER_RANGE;
            for (size_t j = 0; j < i; j++)
                if (order[j] == order[i])
                    return GOAT_INFORMATION_MERGE_ORDER_DUPLICATE;
        }
    } else if (order || count)
        return GOAT_INFORMATION_MERGE_INVALID_INPUT;
    yyjson_mut_doc *doc = yyjson_doc_mut_copy(plan->information, NULL);
    if (!doc)
        return GOAT_INFORMATION_MERGE_MEMORY;
    bool ok = true;
    if (plan->needs_order) {
        yyjson_mut_val *array = yyjson_mut_obj_get(yyjson_mut_doc_get_root(doc), "authors");
        yyjson_mut_arr_clear(array);
        for (size_t i = 0; ok && i < count; i++) {
            yyjson_mut_val *value = yyjson_val_mut_copy(doc, yyjson_arr_get(authors, order[i]));
            ok = value && yyjson_mut_arr_append(array, value);
        }
    }
    if (ok)
        *out = yyjson_mut_doc_imut_copy(doc, NULL);
    yyjson_mut_doc_free(doc);
    return *out ? GOAT_INFORMATION_MERGE_OK : GOAT_INFORMATION_MERGE_MEMORY;
}

goat_information_merge_error goat_information_merge_parse_order(const char *line,
                                                                size_t length,
                                                                size_t candidate_count,
                                                                size_t *order) {
    if (!line || !candidate_count || !order)
        return GOAT_INFORMATION_MERGE_INVALID_INPUT;
    if (length && line[length - 1] == '\n')
        length--;
    if (length && line[length - 1] == '\r')
        length--;
    size_t position = 0, count = 0;
    while (position < length) {
        if (line[position] == ' ' || line[position] == '\t') {
            position++;
            continue;
        }
        size_t value = 0;
        bool digits = false, overflow = false;
        while (position < length && line[position] != ' ' && line[position] != '\t') {
            unsigned char ch = (unsigned char)line[position++];
            if (ch < '0' || ch > '9')
                return GOAT_INFORMATION_MERGE_ORDER_NUMBER;
            digits = true;
            size_t digit = (size_t)(ch - '0');
            if (value > (SIZE_MAX - digit) / 10)
                overflow = true;
            else
                value = value * 10 + digit;
        }
        if (!digits || overflow || !value || value > candidate_count)
            return GOAT_INFORMATION_MERGE_ORDER_RANGE;
        value--;
        for (size_t i = 0; i < count; i++)
            if (order[i] == value)
                return GOAT_INFORMATION_MERGE_ORDER_DUPLICATE;
        if (count == candidate_count)
            return GOAT_INFORMATION_MERGE_ORDER_MISSING;
        order[count++] = value;
    }
    return !count                     ? GOAT_INFORMATION_MERGE_ORDER_EMPTY
           : count != candidate_count ? GOAT_INFORMATION_MERGE_ORDER_MISSING
                                      : GOAT_INFORMATION_MERGE_OK;
}

const char *goat_information_merge_error_string(goat_information_merge_error error) {
    switch (error) {
        case GOAT_INFORMATION_MERGE_OK:
            return "ok";
        case GOAT_INFORMATION_MERGE_INVALID_INPUT:
            return "invalid module information input";
        case GOAT_INFORMATION_MERGE_CONFLICT:
            return "conflicting module information";
        case GOAT_INFORMATION_MERGE_MEMORY:
            return "information allocation failed";
        case GOAT_INFORMATION_MERGE_ORDER_REQUIRED:
            return "interactive author order is required";
        case GOAT_INFORMATION_MERGE_ORDER_EMPTY:
            return "empty author order";
        case GOAT_INFORMATION_MERGE_ORDER_MISSING:
            return "every author number is required exactly once";
        case GOAT_INFORMATION_MERGE_ORDER_DUPLICATE:
            return "duplicate author number";
        case GOAT_INFORMATION_MERGE_ORDER_NUMBER:
            return "author numbers must be decimal integers";
        case GOAT_INFORMATION_MERGE_ORDER_RANGE:
            return "author number is out of range";
        case GOAT_INFORMATION_MERGE_INPUT_ENDED:
            return "author order input ended before a valid choice";
        case GOAT_INFORMATION_MERGE_IO:
            return "author order IO failed";
    }
    return "unknown information merge error";
}

static bool message(FILE *stream, const char *text) {
    return FPUTS(text, stream) >= 0;
}

goat_information_merge_error goat_information_merge_interactive(const goat_information_merge *plan,
                                                                bool interactive,
                                                                FILE *input,
                                                                FILE *messages,
                                                                yyjson_doc **out) {
    if (out)
        *out = NULL;
    if (!plan || !out)
        return GOAT_INFORMATION_MERGE_INVALID_INPUT;
    if (!plan->needs_order)
        return goat_information_merge_finish(plan, NULL, 0, out);
    if (!interactive)
        return GOAT_INFORMATION_MERGE_ORDER_REQUIRED;
    if (!input || !messages)
        return GOAT_INFORMATION_MERGE_INVALID_INPUT;
    const yyjson_val *authors = goat_information_merge_candidates(plan);
    size_t count = yyjson_arr_size(authors);
    if (count > SIZE_MAX / sizeof(size_t))
        return GOAT_INFORMATION_MERGE_MEMORY;
    size_t *order = malloc(count * sizeof(*order));
    char *line = NULL;
    size_t capacity = 0;
    goat_information_merge_error error = GOAT_INFORMATION_MERGE_MEMORY;
    if (!order)
        goto done;
    for (size_t i = 0; i < count; i++) {
        char *json = NULL;
        size_t length = 0;
        goat_module_json_error json_error =
            goat_json_canonical(yyjson_arr_get(authors, i), &json, &length);
        if (json_error)
            goto done;
        bool ok = FPRINTF(messages, "%zu. ", i + 1) >= 0
                  && FWRITE(json, 1, length, messages) == length && FPUTC('\n', messages) != EOF;
        goat_module_json_free(json);
        if (!ok) {
            error = GOAT_INFORMATION_MERGE_IO;
            goto done;
        }
    }
    for (;;) {
        if (!message(messages, "Choose author order (all numbers, space separated): ")
            || FFLUSH(messages)) {
            error = GOAT_INFORMATION_MERGE_IO;
            goto done;
        }
        size_t length = 0;
        int ch;
        while ((ch = FGETC(input)) != EOF) {
            if (length == capacity) {
                size_t next = capacity ? capacity <= SIZE_MAX / 2 ? capacity * 2 : SIZE_MAX : 128;
                if (next <= capacity) {
                    error = GOAT_INFORMATION_MERGE_MEMORY;
                    goto done;
                }
                char *grown = realloc(line, next);
                if (!grown) {
                    error = GOAT_INFORMATION_MERGE_MEMORY;
                    goto done;
                }
                line = grown;
                capacity = next;
            }
            line[length++] = (char)ch;
            if (ch == '\n')
                break;
        }
        if (FERROR(input)) {
            error = GOAT_INFORMATION_MERGE_IO;
            goto done;
        }
        if (!length && ch == EOF) {
            error = GOAT_INFORMATION_MERGE_INPUT_ENDED;
            goto done;
        }
        error = goat_information_merge_parse_order(line, length, count, order);
        if (!error) {
            error = goat_information_merge_finish(plan, order, count, out);
            goto done;
        }
        if (!message(messages, goat_information_merge_error_string(error))
            || FPUTC('\n', messages) == EOF) {
            error = GOAT_INFORMATION_MERGE_IO;
            goto done;
        }
        if (ch == EOF) {
            error = GOAT_INFORMATION_MERGE_INPUT_ENDED;
            goto done;
        }
    }
done:
    free(line);
    free(order);
    return error;
}
