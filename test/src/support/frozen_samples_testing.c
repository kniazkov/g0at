/** @file frozen_samples_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Read-only integrity gate for the independently frozen #330 inputs.
 * The manifest anchor pins ALL provenance, formatting, lengths and digests, not
 * just values read from a potentially altered manifest. Historical source hashes
 * describe the baseline, not the current (now migrated) source tree. This is an
 * integrity check, not a reimplementation of CPython Random or zlib reproduction.
 */
#include "lib/files/file_system.h"
#include "lib/hash/sha256.h"
#include "lib/memory/allocate.h"
#include "test/support/execution.h"
#include "yyjson.h"

#include <stdlib.h>
#include <string.h>

static const char manifest_hash[] =
    "a6a8db24abaf27ae8d15c4dc2c7c99b395ab5c3533ef25cdcdb87721a7fb1994";
static const char *names[] = {"abi-guard-pairs.json",
                              "canonical-key-orders.json",
                              "container-damage-base.bin",
                              "container-damage.json",
                              "saturation-pairs.json",
                              "writer-native-order.json",
                              "writer-payload.bin",
                              "zip-damage-base-windows.bin",
                              "zip-damage-base.bin",
                              "zip-damage.json",
                              "zip-payload.bin",
                              "manifest.json"};
#define FILE_COUNT (sizeof(names) / sizeof(names[0]))
/* The largest pinned input is 644318 bytes. Bound malformed-file reads too. */
#define FROZEN_INPUT_LIMIT (1024 * 1024)

typedef struct {
    unsigned char *data;
    size_t size;
} bytes_t;

static char *path_join(const char *directory, const char *name) {
    size_t size = strlen(directory) + strlen(name) + 2;
    char *path = malloc(size);
    if (path)
        snprintf(path, size, "%s/%s", directory, name);
    return path;
}

static bool read_file(const char *directory, const char *name, bytes_t *result) {
    *result = (bytes_t){0};
    char *path = path_join(directory, name);
    if (!path)
        return false;
    void *owned = NULL;
    size_t size = 0;
    bool ok = read_fs_regular_file(path, FROZEN_INPUT_LIMIT, &owned, &size, NULL);
    free(path);
    if (!ok)
        return false;
    /* The filesystem API uses the tracked allocator; local buffers use malloc. */
    unsigned char *data = malloc(size ? size : 1);
    if (data)
        memcpy(data, owned, size);
    FREE(owned);
    if (!data)
        return false;
    result->data = data;
    result->size = size;
    return true;
}

static bool hash_matches(const bytes_t *bytes, const char *expected) {
    if (!expected || strlen(expected) != 64)
        return false;
    uint8_t digest[SHA256_SIZE];
    char hex[65];
    static const char digits[] = "0123456789abcdef";
    sha256(bytes->data, bytes->size, digest);
    for (size_t i = 0; i < SHA256_SIZE; ++i) {
        hex[2 * i] = digits[digest[i] >> 4];
        hex[2 * i + 1] = digits[digest[i] & 15];
    }
    hex[64] = 0;
    return strcmp(hex, expected) == 0;
}

static bool damaged_inputs(const char *directory,
                           yyjson_val *manifest,
                           const char *base_name,
                           const char *recipe_name,
                           const char *hash_key,
                           size_t count) {
    bytes_t base = {0}, recipe = {0};
    yyjson_doc *doc = NULL;
    unsigned char *damaged = NULL;
    bool ok = false;
    if (!read_file(directory, base_name, &base) || !read_file(directory, recipe_name, &recipe))
        goto done;
    doc = yyjson_read((const char *)recipe.data, recipe.size, 0);
    if (!doc)
        goto done;
    yyjson_val *cases = yyjson_doc_get_root(doc), *hashes = yyjson_obj_get(manifest, hash_key);
    if (!yyjson_is_arr(cases) || yyjson_arr_size(cases) != count || !yyjson_is_arr(hashes)
        || yyjson_arr_size(hashes) != count)
        goto done;
    damaged = malloc(base.size);
    if (!damaged)
        goto done;
    for (size_t i = 0; i < count; ++i) {
        memcpy(damaged, base.data, base.size);
        yyjson_val *mutations = yyjson_arr_get(cases, i);
        if (!yyjson_is_arr(mutations) || yyjson_arr_size(mutations) != 3)
            goto done;
        for (size_t j = 0; j < 3; ++j) {
            yyjson_val *pair = yyjson_arr_get(mutations, j);
            if (!yyjson_is_arr(pair) || yyjson_arr_size(pair) != 2)
                goto done;
            yyjson_val *offset = yyjson_arr_get(pair, 0), *mask = yyjson_arr_get(pair, 1);
            if (!yyjson_is_uint(offset) || !yyjson_is_uint(mask))
                goto done;
            uint64_t at = yyjson_get_uint(offset), bits = yyjson_get_uint(mask);
            if (at >= base.size || !bits || bits > 128 || (bits & (bits - 1)))
                goto done;
            damaged[at] ^= (unsigned char)bits;
        }
        bytes_t input = {damaged, base.size};
        if (!hash_matches(&input, yyjson_get_str(yyjson_arr_get(hashes, i))))
            goto done;
    }
    ok = true;
done:
    free(damaged);
    yyjson_doc_free(doc);
    free(base.data);
    free(recipe.data);
    return ok;
}

static bool check(const char *directory, bool diagnostics) {
    bytes_t manifest = {0};
    yyjson_doc *doc = NULL;
    bool ok = false;
    const char *failure = "manifest.json";
    if (!read_file(directory, failure, &manifest) || !hash_matches(&manifest, manifest_hash))
        goto done;
    doc = yyjson_read((const char *)manifest.data, manifest.size, 0);
    if (!doc)
        goto done;
    yyjson_val *root = yyjson_doc_get_root(doc), *samples = yyjson_obj_get(root, "samples");
    if (!yyjson_is_arr(samples) || yyjson_arr_size(samples) != FILE_COUNT - 1)
        goto done;
    for (size_t i = 0; i < FILE_COUNT - 1; ++i) {
        yyjson_val *entry = yyjson_arr_get(samples, i);
        const char *name = yyjson_get_str(yyjson_obj_get(entry, "path"));
        if (!name || strcmp(name, names[i]))
            goto done;
        failure = names[i];
        bytes_t bytes = {0};
        if (!read_file(directory, failure, &bytes))
            goto done;
        yyjson_val *size = yyjson_obj_get(entry, "bytes");
        bool matches = yyjson_is_uint(size) && yyjson_get_uint(size) == bytes.size
                       && hash_matches(&bytes, yyjson_get_str(yyjson_obj_get(entry, "sha256")));
        free(bytes.data);
        if (!matches)
            goto done;
    }
    failure = "materialized damaged inputs";
    ok = damaged_inputs(directory,
                        root,
                        "zip-damage-base.bin",
                        "zip-damage.json",
                        "zip_damaged_inputs_sha256",
                        60)
         && damaged_inputs(directory,
                           root,
                           "zip-damage-base-windows.bin",
                           "zip-damage.json",
                           "zip-windows_damaged_inputs_sha256",
                           60)
         && damaged_inputs(directory,
                           root,
                           "container-damage-base.bin",
                           "container-damage.json",
                           "container_damaged_inputs_sha256",
                           100);
done:
    if (!ok && diagnostics)
        fprintf(stderr, "Frozen samples differ: %s (%s)\n", failure, directory);
    yyjson_doc_free(doc);
    free(manifest.data);
    return ok;
}

static bool write_file(const char *directory, const char *name, const bytes_t *bytes) {
    char *path = path_join(directory, name);
    if (!path)
        return false;
    FILE *file = FOPEN(path, "wb");
    free(path);
    if (!file)
        return false;
    bool ok = fwrite(bytes->data, 1, bytes->size, file) == bytes->size;
    if (fclose(file) != 0)
        ok = false;
    return ok;
}

/* Exercise materialization itself, bypassing the outer fixture digest gate. */
static bool damaged_negative_controls(const char *source, const char *work) {
    static const char *bases[] = {"zip-damage-base.bin",
                                  "zip-damage-base-windows.bin",
                                  "container-damage-base.bin"};
    static const char *recipes[] = {"zip-damage.json", "zip-damage.json", "container-damage.json"};
    static const char *keys[] = {"zip_damaged_inputs_sha256",
                                 "zip-windows_damaged_inputs_sha256",
                                 "container_damaged_inputs_sha256"};
    bytes_t manifest = {0};
    if (!read_file(source, "manifest.json", &manifest))
        return false;
    yyjson_doc *doc = yyjson_read((const char *)manifest.data, manifest.size, 0);
    free(manifest.data);
    if (!doc)
        return false;
    bool ok = true;
    for (size_t i = 0; ok && i < 3; ++i) {
        bytes_t base = {0};
        if (!read_file(source, bases[i], &base)) {
            ok = false;
            break;
        }
        base.data[0] ^= 1;
        ok = write_file(work, bases[i], &base)
             && !damaged_inputs(work,
                                yyjson_doc_get_root(doc),
                                bases[i],
                                recipes[i],
                                keys[i],
                                i == 2 ? 100 : 60);
        base.data[0] ^= 1;
        if (!write_file(work, bases[i], &base))
            ok = false;
        free(base.data);
    }
    yyjson_doc_free(doc);
    return ok;
}

static bool negative_controls(scenario_run_t *run) {
    const char *source = scenario_data_directory(scenario_run_context(run));
    const char *work = scenario_work_directory(scenario_run_context(run));
    for (size_t i = 0; i < FILE_COUNT; ++i) {
        bytes_t bytes = {0};
        if (!read_file(source, names[i], &bytes))
            return false;
        bool ok = write_file(work, names[i], &bytes);
        free(bytes.data);
        if (!ok)
            return false;
    }
    if (!check(work, true))
        return false;
    for (size_t i = 0; i < FILE_COUNT; ++i) {
        scenario_checkpoint(run);
        bytes_t saved = {0}, actual = {0};
        if (!read_file(source, names[i], &saved))
            return false;
        bytes_t corrupt = {(unsigned char *)"corrupt", 7};
        bool ok = write_file(work, names[i], &corrupt) && !check(work, false)
                  && read_file(work, names[i], &actual) && actual.size == corrupt.size
                  && memcmp(actual.data, corrupt.data, corrupt.size) == 0;
        free(actual.data);
        char *path = path_join(work, names[i]);
        if (!path)
            ok = false;
        if (ok)
            ok = remove_fs_entry(path, NULL) && !check(work, false);
        /* A rejected missing input must remain missing (no repair side effect). */
        fs_info_t info;
        fs_error_t error = {0};
        if (ok)
            ok = !inspect_fs_path(path, false, &info, &error) && error.status == FS_NOT_FOUND;
        clear_fs_error(&error);
        if (ok)
            ok = write_file(work, names[i], &saved);
        free(path);
        free(saved.data);
        if (!ok) {
            fprintf(stderr, "Negative control failed: %s\n", names[i]);
            return false;
        }
    }
    return damaged_negative_controls(source, work) && check(work, true) && check(source, true);
}

static void self_test(scenario_run_t *run, void *unused) {
    (void)unused;
    scenario_require(run,
                     negative_controls(run),
                     "corruption/missing fixture rejection without repairs");
    puts("Corrupted and missing fixtures rejected without repairs (12 files, 24 file controls, 3 "
         "materialization controls)");
}

static int run_main(int argc, char **argv) {
    const char *data = NULL, *build = NULL;
    bool self = false, verify = false, keep = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--check") && !verify && !self)
            verify = true;
        else if (!strcmp(argv[i], "--self-test") && !verify && !self)
            self = true;
        else if ((!strcmp(argv[i], "--data") || !strcmp(argv[i], "--output")) && i + 1 < argc
                 && !data)
            data = argv[++i];
        else if (!strcmp(argv[i], "--build") && i + 1 < argc && !build)
            build = argv[++i];
        else if (scenario_keep_work_argument(argv[i]) && !keep)
            keep = true;
        else
            goto usage;
    }
    if (!data || (!verify && !self) || (self && !build))
        goto usage;
    if (!check(data, true))
        return 1;
    if (verify) {
        puts("Frozen Python inputs verified: 11 fixtures, 6 pinned historical source hashes, 220 "
             "damaged inputs");
        return 0;
    }
    scenario_options_t options = {"frozen-samples", build, data, keep, stderr};
    return scenario_execute(&options, 30000, self_test, NULL) ? 0 : 1;
usage:
    fprintf(stderr,
            "Usage: %s (--check | --self-test) --data DIR [--build DIR] [--keep-work]\n",
            argv[0]);
    return 2;
}

int main(int argc, char **argv) {
#ifdef _WIN32
    if (!fs_arguments(&argc, &argv, NULL))
        return 1;
#endif
    int result = run_main(argc, argv);
#ifdef _WIN32
    free_fs_arguments(argc, argv);
#endif
    return result;
}
