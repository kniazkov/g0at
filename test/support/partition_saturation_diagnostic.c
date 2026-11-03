/** @file partition_saturation_diagnostic.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Strict byte-preserving partition of the frozen arithmetic diagnostic driver.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCKS 1196
#define CHUNK 64
#define CAPACITY 1048576
static const char prefix[] = "#include \"saturation.c\"\n#include \"integer_math.h\"\n#include "
                             "<assert.h>\nint main(void) {\n";
static const char headers[] =
    "#include \"native_runtime.h\"\n#include \"integer_math.h\"\n#include <assert.h>\n";
static const char suffix[] = "return 0;\n}";
static const char *assertions[] = {"assert(g_i64_add(a,b)==",
                                   "assert(add_int64_saturating(a,b)==",
                                   "assert(g_i64_sub(a,b)==",
                                   "assert(subtract_int64_saturating(a,b)==",
                                   "assert(g_i64_mul(a,b)==",
                                   "assert(multiply_int64_saturating(a,b)=="};

static FILE *open_output(const char *directory, const char *name, const char *mode) {
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/%s", directory, name);
    return length < 0 || (size_t)length >= sizeof(path) ? NULL : fopen(path, mode);
}

static bool write_bytes(FILE *file, const void *bytes, size_t size) {
    return fwrite(bytes, 1, size, file) == size;
}

static int fail(const char *message) {
    fprintf(stderr, "Partition rejected: %s\n", message);
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 3)
        return fail("expected INPUT OUTPUT_DIRECTORY");
    FILE *input = fopen(argv[1], "rb");
    if (!input)
        return fail("cannot open input");
    char *bytes = malloc(CAPACITY + 1);
    if (!bytes) {
        fclose(input);
        return fail("allocation");
    }
    size_t size = fread(bytes, 1, CAPACITY, input);
    bool read_ok = !ferror(input) && size < CAPACITY;
    if (fclose(input) || !read_ok || memchr(bytes, 0, size))
        return fail("invalid input bytes");
    bytes[size] = 0;
    size_t prefix_size = sizeof(prefix) - 1, suffix_size = sizeof(suffix) - 1;
    if (size < prefix_size + suffix_size || memcmp(bytes, prefix, prefix_size)
        || memcmp(bytes + size - suffix_size, suffix, suffix_size))
        return fail("unexpected driver wrapper");
    char *begin[BLOCKS + 1], *at = bytes + prefix_size;
    for (size_t block = 0; block < BLOCKS; block++) {
        begin[block] = at;
        const char opening[] = "{ volatile int64_t a=";
        if (strncmp(at, opening, sizeof(opening) - 1))
            return fail("operand block");
        char *line = strchr(at, '\n');
        char *second = strstr(at, ",b=");
        if (!line || line == at || line[-1] != ';' || !second || second >= line)
            return fail("operand declaration");
        at = line + 1;
        for (size_t assertion = 0; assertion < 6; assertion++) {
            size_t length = strlen(assertions[assertion]);
            if (strncmp(at, assertions[assertion], length))
                return fail("assertion order or kind");
            line = strchr(at, '\n');
            if (!line || line - at < (ptrdiff_t)(length + 3) || line[-2] != ')' || line[-1] != ';')
                return fail("assertion line");
            at = line + 1;
        }
        if (strncmp(at, "}\n", 2))
            return fail("block end");
        at += 2;
    }
    begin[BLOCKS] = at;
    if (at != bytes + size - suffix_size)
        return fail("block count or trailing data");
    size_t assertion_count = 0;
    for (char *found = strstr(bytes + prefix_size, "assert("); found;
         found = strstr(found + 7, "assert("))
        assertion_count++;
    if (assertion_count != BLOCKS * 6)
        return fail("assertion count");
    FILE *main_file = open_output(argv[2], "main.c", "wb");
    FILE *proof = open_output(argv[2], "reconstructed-driver.c", "wb");
    if (!main_file || !proof)
        return fail("output creation");
    if (!write_bytes(main_file, bytes, prefix_size - strlen("int main(void) {\n"))
        || !write_bytes(proof, prefix, prefix_size))
        return fail("header write");
    size_t chunks = (BLOCKS + CHUNK - 1) / CHUNK;
    for (size_t chunk = 0; chunk < chunks; chunk++) {
        char name[32], declaration[64];
        snprintf(name, sizeof(name), "part%03zu.c", chunk);
        int length =
            snprintf(declaration, sizeof(declaration), "void check_part_%zu(void) {\n", chunk);
        size_t first = chunk * CHUNK, last = first + CHUNK;
        if (last > BLOCKS)
            last = BLOCKS;
        size_t body_size = (size_t)(begin[last] - begin[first]);
        FILE *part = open_output(argv[2], name, "wb");
        if (!part || !write_bytes(part, headers, sizeof(headers) - 1)
            || !write_bytes(part, declaration, (size_t)length)
            || !write_bytes(part, begin[first], body_size) || !write_bytes(part, "}\n", 2))
            return fail("chunk write");
        if (fclose(part))
            return fail("chunk close");
        /* Reopen actual emitted bytes: the proof must not merely copy the input. */
        part = open_output(argv[2], name, "rb");
        size_t emitted_size = sizeof(headers) - 1 + (size_t)length + body_size + 2;
        char *emitted = malloc(emitted_size + 1);
        if (!part || !emitted || fread(emitted, 1, emitted_size + 1, part) != emitted_size
            || ferror(part))
            return fail("chunk verification read");
        if (fclose(part) || memcmp(emitted, headers, sizeof(headers) - 1)
            || memcmp(emitted + sizeof(headers) - 1, declaration, (size_t)length)
            || memcmp(emitted + emitted_size - 2, "}\n", 2)
            || memcmp(emitted + sizeof(headers) - 1 + length, begin[first], body_size))
            return fail("chunk verification mismatch");
        if (!write_bytes(proof, emitted + sizeof(headers) - 1 + length, body_size))
            return fail("proof write");
        free(emitted);
        if (fprintf(main_file, "void check_part_%zu(void);\n", chunk) < 0)
            return fail("declaration write");
    }
    if (fputs("int main(void) {\n", main_file) == EOF)
        return fail("main write");
    for (size_t chunk = 0; chunk < chunks; chunk++)
        if (fprintf(main_file, "check_part_%zu();\n", chunk) < 0)
            return fail("call write");
    if (!write_bytes(main_file, suffix, suffix_size) || !write_bytes(proof, suffix, suffix_size))
        return fail("suffix write");
    bool closed = fclose(main_file) == 0;
    if (fclose(proof) || !closed)
        return fail("output close");
    free(bytes);
    printf("blocks=%d assertions=%d chunks=%zu raw_block_bytes_equal=1\n",
           BLOCKS,
           BLOCKS * 6,
           chunks);
    return 0;
}
