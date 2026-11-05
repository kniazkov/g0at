/** @file format_vectors.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent, reproducible format-v1 vector generation and byte checking.
 */
#include "lib/files/file_system.h"
#include "test/support/format_binary.h"
#include "test/support/format_zip.h"
#include "zlib.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef GOAT_FORMAT_ROOT
#    define GOAT_FORMAT_ROOT "."
#endif

static char error[512];
static unsigned count;
static bool writing;
static const char *root;
static yyjson_mut_val *source;

static void fail(const char *message) {
    fprintf(stderr, "format-vectors: %s%s%s\n", message, *error ? ": " : "", error);
    exit(EXIT_FAILURE);
}

static void *needed(void *p) {
    if (!p)
        fail("allocation or construction failed");
    return p;
}

static char *join(const char *a, const char *b) {
    size_t n = strlen(a) + strlen(b) + 2;
    char *p = needed(malloc(n));
    snprintf(p, n, "%s/%s", a, b);
#ifdef _WIN32
    for (char *q = p; *q; ++q)
        if (*q == '\\')
            *q = '/';
#endif
    return p;
}

static void directory(char *path) {
    fs_error_t e = {0};
    fs_info_t info;
    if (inspect_fs_path(path, true, &info, &e)) {
        clear_fs_error(&e);
        if (info.kind != FS_DIRECTORY)
            fail(path);
        return;
    }
    if (e.status != FS_NOT_FOUND)
        fail(path);
    clear_fs_error(&e);
    char *slash = strrchr(path, '/');
    if (slash && slash != path) {
        char *end = slash;
#ifdef _WIN32
        /* Keep a drive's root slash: C:/, never the drive-relative C:. */
        if (slash == path + 2 && path[1] == ':') {
            if (!slash[1])
                fail(path);
            end++;
        }
#endif
        char saved = *end;
        *end = 0;
        directory(path);
        *end = saved;
    }
    bool ok = create_fs_directory(path, &e);
    clear_fs_error(&e);
    if (!ok)
        fail(path);
}

static void parents(char *path) {
    char *slash = strrchr(path, '/');
    if (!slash)
        fail("destination has no parent");
    *slash = 0;
    directory(path);
    *slash = '/';
}

static unsigned char *read_bytes(const char *path, size_t *size) {
    FILE *f = FOPEN(path, "rb");
    if (!f)
        fail(path);
    if (fseek(f, 0, SEEK_END))
        fail(path);
    long length = ftell(f);
    if (length < 0 || length > 16 * 1024 * 1024 || fseek(f, 0, SEEK_SET))
        fail(path);
    unsigned char *data = needed(malloc((size_t)length + 1));
    *size = (size_t)length;
    bool ok = fread(data, 1, *size, f) == *size;
    if (fclose(f) || !ok)
        fail(path);
    data[*size] = 0;
    return data;
}

static void emit(const char *relative, const void *data, size_t size) {
    char *base = join(root, "docs/module-format-v1");
    char *path = join(base, relative);
    free(base);
    if (writing) {
        parents(path);
        FILE *f = FOPEN(path, "wb");
        if (!f)
            fail(path);
        bool ok = fwrite(data, 1, size, f) == size;
        if (fclose(f) || !ok)
            fail(path);
    } else {
        size_t actual_size;
        unsigned char *actual = read_bytes(path, &actual_size);
        if (actual_size != size || memcmp(actual, data, size)) {
            snprintf(error, sizeof(error), "stale generated file");
            fail(path);
        }
        free(actual);
    }
    count++;
    free(path);
}

static void text(const char *path, const char *value) {
    emit(path, value, strlen(value));
}

static char *hex(const void *data, size_t size) {
    const unsigned char *bytes = data;
    char *result = needed(malloc(2 * size + 1));
    for (size_t i = 0; i < size; ++i) {
        result[2 * i] = "0123456789abcdef"[bytes[i] >> 4];
        result[2 * i + 1] = "0123456789abcdef"[bytes[i] & 15];
    }
    result[2 * size] = 0;
    return result;
}

static void hex_file(const char *path, const void *bytes, size_t size) {
    char *encoded = hex(bytes, size);
    char *line = needed(realloc(encoded, size * 2 + 2));
    line[size * 2] = '\n';
    emit(path, line, size * 2 + 1);
    free(line);
}

static yyjson_mut_doc *parse(const char *json) {
    return needed(format_json_parse(json, strlen(json), error, sizeof(error)));
}

static void
put_string(yyjson_mut_doc *doc, yyjson_mut_val *object, const char *key, const char *value) {
    if (!yyjson_mut_obj_put(object, yyjson_mut_str(doc, key), yyjson_mut_strcpy(doc, value)))
        fail("set string");
}

static char *canonical(yyjson_mut_val *value) {
    size_t size;
    return needed(format_json_canonical(value, &size));
}

static void json_file(const char *path, yyjson_mut_val *value, bool newline) {
    char *s = canonical(value);
    size_t size = strlen(s);
    if (newline) {
        s = needed(realloc(s, size + 2));
        s[size++] = '\n';
    }
    emit(path, s, size);
    free(s);
}

static void pretty_file(const char *path, yyjson_mut_val *value) {
    size_t size;
    char *s = needed(yyjson_mut_val_write(value, YYJSON_WRITE_PRETTY_TWO_SPACES, &size));
    s = needed(realloc(s, size + 1));
    s[size++] = '\n';
    emit(path, s, size);
    free(s);
}

static void
hash_module(yyjson_mut_doc *doc, const unsigned char *bytes, size_t size, char hash[65]) {
    yyjson_mut_val *v = yyjson_mut_doc_get_root(doc);
    if (!format_binary_main_hash(v, source, bytes, size, hash, error, sizeof(error)))
        fail("main hash");
    put_string(doc, v, "mainHash", hash);
}

static void vectors(void) {
    if (strcmp(zlibVersion(), "1.3.2"))
        fail("golden ZIP writer requires zlib 1.3.2");
    size_t bn, in;
    unsigned char *b = needed(format_binary_minimal(&bn));
    unsigned char *imports = needed(format_binary_importing(&in));
    yyjson_mut_doc *m =
        parse("{\"formatVersion\":1,\"bytecodeVersion\":1,\"name\":\"empty\","
              "\"version\":\"1.0\",\"mainHash\":"
              "\"0000000000000000000000000000000000000000000000000000000000000000\","
              "\"main\":0,\"files\":[{\"name\":\"main.goat\",\"entry\":0,\"external\":false}]}");
    yyjson_mut_val *mv = yyjson_mut_doc_get_root(m);
    char mh[65], h1[65], h2[65], dh[65];
    hash_module(m, b, bn, mh);
    char *mj = canonical(mv);
    yyjson_mut_doc *l1 = parse(mj), *l2 = parse(mj), *dep = parse(mj);
    put_string(l1, yyjson_mut_doc_get_root(l1), "name", "math");
    hash_module(l1, b, bn, h1);
    put_string(l2, yyjson_mut_doc_get_root(l2), "name", "math");
    put_string(l2, yyjson_mut_doc_get_root(l2), "version", "2.0");
    hash_module(l2, b, bn, h2);
    yyjson_mut_val *dv = yyjson_mut_doc_get_root(dep), *deps = yyjson_mut_arr(dep);
    put_string(dep, dv, "name", "two-versions");
    if (!yyjson_mut_obj_add_val(dep, dv, "dependencies", deps))
        fail("dependencies");
    for (unsigned i = 0; i < 2; ++i) {
        yyjson_mut_val *d = yyjson_mut_obj(dep);
        put_string(dep, d, "name", "math");
        put_string(dep, d, "version", i ? "2.0" : "1.0");
        put_string(dep, d, "hash", i ? h2 : h1);
        if (!yyjson_mut_arr_append(deps, d))
            fail("dependency");
    }
    hash_module(dep, imports, in, dh);
    hex_file("vectors/empty.bytecode.hex", b, bn);
    json_file("vectors/empty.module.json", mv, false);
    size_t main_size;
    char *main = needed(format_metadata_main_bytes(mv, source, &main_size, error, sizeof(error)));
    emit("vectors/empty.main.json", main, main_size);
    free(main);
    json_file("vectors/math-1.module.json", yyjson_mut_doc_get_root(l1), true);
    json_file("vectors/math-2.module.json", yyjson_mut_doc_get_root(l2), true);
    json_file("vectors/two-versions.module.json", dv, true);
    hex_file("vectors/two-versions.bytecode.hex", imports, in);
    text("vectors/empty.gmod",
         "{\"formatVersion\":1,\"name\":\"empty\",\"version\":\"1.0\",\"main\":\"main.goat\"}\n");
    text("vectors/unicode.json", "{\"type\":\"string\",\"value\":\"A\\u0000Я😀\"}\n");
    format_zip_entry_t entries[2];
    format_zip_entry_init(&entries[0], "module.json", mj, strlen(mj), true);
    format_zip_entry_init(&entries[1], "bytecode.bin", b, bn, true);
    size_t an;
    unsigned char *archive =
        needed(format_zip_build(entries, 2, NULL, 0, false, &an, error, sizeof(error)));
    hex_file("vectors/empty.gbin.hex", archive, an);
    yyjson_mut_doc *hashes = parse("{}");
    yyjson_mut_val *hv = yyjson_mut_doc_get_root(hashes);
    put_string(hashes, hv, "mainHash", mh);
    char digest[65];
    format_binary_sha256(b, bn, digest);
    put_string(hashes, hv, "bytecodeSha256", digest);
    format_binary_sha256(archive, an, digest);
    put_string(hashes, hv, "archiveSha256", digest);
    size_t hn;
    unsigned char *input =
        needed(format_metadata_hash_input(mv, source, b, bn, &hn, error, sizeof(error)));
    char *input_hex = hex(input, hn);
    put_string(hashes, hv, "hashInputHex", input_hex);
    pretty_file("vectors/hashes.json", hv);
    free(input_hex);
    free(input);
    yyjson_mut_doc_free(hashes);
    text("vectors/invalid-duplicate-key.json", "{\"formatVersion\":1,\"formatVersion\":1}\n");
    text("vectors/invalid-integer-spelling.json", "{\"formatVersion\":1e0}\n");
    text("vectors/invalid-surrogate.json", "{\"description\":\"\\ud800\"}\n");
    yyjson_mut_val *file = yyjson_mut_arr_get(yyjson_mut_obj_get(mv, "files"), 0);
    if (!yyjson_mut_obj_put(file, yyjson_mut_str(m, "entry"), yyjson_mut_uint(m, 1)))
        fail("entry");
    json_file("vectors/invalid-entry.module.json", mv, true);
    yyjson_mut_doc *native = parse(
        "{\"formatVersion\":1,\"module\":\"native-example\",\"version\":\"1.0\","
        "\"mainHash\":\"1111111111111111111111111111111111111111111111111111111111111111\","
        "\"goatAbiVersion\":1,\"target\":{\"os\":\"linux\",\"minimumOs\":[5,4],\"architecture\":"
        "\"x86_64\","
        "\"bits\":64,\"endian\":\"little\",\"cpuFeatures\":[\"sse2\"],\"runtime\":{\"family\":"
        "\"glibc\",\"minimumVersion\":[2,28]}},"
        "\"specializations\":[{\"specializationId\":1,\"functionId\":1,\"symbol\":\"goat_fn_1_v1\","
        "\"signature\":{\"id\":\"goat.adapter.v1\",\"arguments\":[\"integer\",\"integer\"],"
        "\"result\":\"integer\"},"
        "\"when\":{\"argumentTypes\":[\"integer\",\"integer\"],\"conditions\":[{\"argument\":1,"
        "\"operation\":\"notEqual\","
        "\"constant\":{\"type\":\"integer\",\"value\":0}}]}}]}");
    yyjson_mut_val *nv = yyjson_mut_doc_get_root(native);
    if (!format_metadata_normalize(native, nv, "native", source, error, sizeof(error)))
        fail("native normalize");
    json_file("vectors/native-description.json", nv, true);
    char *nj = canonical(nv);
    size_t nn = strlen(nj);
    unsigned char *envelope = needed(calloc(nn + 17, 1));
    memcpy(envelope, "GOATND01", 8);
    for (unsigned i = 0; i < 8; ++i)
        envelope[8 + i] = (unsigned char)((uint64_t)nn >> (8 * i));
    memcpy(envelope + 16, nj, nn);
    hex_file("vectors/native-envelope.hex", envelope, nn + 17);
    json_file("vectors/target.json", yyjson_mut_obj_get(nv, "target"), true);
    emit("vectors/empty.gbin", archive, an);
    emit("vectors/empty.bytecode.bin", b, bn);
    emit("vectors/two-versions.bytecode.bin", imports, in);
    free(nj);
    free(envelope);
    free(archive);
    free(mj);
    free(imports);
    free(b);
    yyjson_mut_doc_free(native);
    yyjson_mut_doc_free(dep);
    yyjson_mut_doc_free(l2);
    yyjson_mut_doc_free(l1);
    yyjson_mut_doc_free(m);
}

static void schemas(void) {
    const char *kinds[] = {"gmod", "module", "target", "native"};
    for (unsigned i = 0; i < 4; ++i) {
        yyjson_mut_doc *doc = parse("{}");
        yyjson_mut_val *v = yyjson_mut_doc_get_root(doc);
        put_string(doc, v, "$schema", "https://json-schema.org/draft/2020-12/schema");
        char id[160], path[64];
        snprintf(id,
                 sizeof(id),
                 "https://github.com/kniazkov/g0at/docs/module-format-v1/%s.schema.json",
                 kinds[i]);
        snprintf(path, sizeof(path), "%s.schema.json", kinds[i]);
        put_string(doc, v, "$id", id);
        yyjson_mut_val *schema = yyjson_mut_obj_get(yyjson_mut_obj_get(source, "ROOTS"), kinds[i]);
        if (!yyjson_mut_is_obj(schema))
            fail("missing schema root");
        size_t j, n;
        yyjson_mut_val *k, *value;
        yyjson_mut_obj_foreach(schema, j, n, k, value) {
            if (!yyjson_mut_obj_add(v,
                                    yyjson_mut_val_mut_copy(doc, k),
                                    yyjson_mut_val_mut_copy(doc, value)))
                fail("schema field");
        }
        if (!yyjson_mut_obj_add_val(
                doc,
                v,
                "$defs",
                yyjson_mut_val_mut_copy(doc, yyjson_mut_obj_get(source, "DEFS"))))
            fail("schema definitions");
        pretty_file(path, v);
        yyjson_mut_doc_free(doc);
    }
}

static void opcodes(void) {
    /* Format-v1 wire assignments, deliberately independent of the VM enum. */
    char names[] =
        "NOP ARG END JUMP JIF POP DUP NIL TRUE FALSE ILOAD32 ILOAD64 RLOAD SLOAD CLOAD "
        "VLOAD VAR CONST STORE GETPROP SETPROP UPLUS UMINUS INC DEC LNOT BOOL BNOT LAND LOR "
        "BAND BOR BXOR SHL SHR ADD SUB MUL DIVIDE MODULO POWER LESS LEQ GREATER GREQ EQUAL "
        "DIFF FUNC CALL CALLM RET ENTER LEAVE RESTORE TRY THROW GETINDEX SETINDEX ARRAY "
        "INHERIT LOOP_ENTER LOOP_JUMP DUPN ROTATE SNAPSHOT ITER_NEXT FINALLY_ENTER "
        "FINALLY_LEAVE FINALLY_END DVAR DCONST BLOAD BSTORE INCLUDE IMPORT MAIN DIR CHECKPROP "
        "CHECKINDEX CHECKVAR CHECKBOUND";
    yyjson_mut_doc *doc = parse("{}");
    unsigned index = 0;
    for (char *p = strtok(names, " "); p; p = strtok(NULL, " ")) {
        if (!yyjson_mut_obj_add_uint(doc, yyjson_mut_doc_get_root(doc), p, index++))
            fail("opcode");
    }
    pretty_file("opcodes.json", yyjson_mut_doc_get_root(doc));
    yyjson_mut_doc_free(doc);
}

int main(int argc, char **argv) {
    fs_error_t fs_error = {0};
    if (!fs_arguments(&argc, &argv, &fs_error))
        fail("UTF-8 arguments");
    clear_fs_error(&fs_error);
    root = GOAT_FORMAT_ROOT;
    const char *source_path = NULL;
    bool explicit_root = false, check = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--write"))
            writing = true;
        else if (!strcmp(argv[i], "--check"))
            check = true;
        else if (!strcmp(argv[i], "--root") && i + 1 < argc) {
            root = argv[++i];
            explicit_root = true;
        } else if (!strcmp(argv[i], "--source") && i + 1 < argc)
            source_path = argv[++i];
        else if (!strcmp(argv[i], "--help")) {
            puts("format_vectors [--check] [--root REPOSITORY] [--source SCHEMA_JSON]\n"
                 "format_vectors --write --root DESTINATION [--source SCHEMA_JSON]");
            free_fs_arguments(argc, argv);
            return 0;
        } else
            fail("unknown or incomplete option");
    }
    if (writing && (check || !explicit_root))
        fail("--write requires explicit --root and excludes --check");
    char *default_source = join(root, "docs/module-format-v1/schema-source.json");
    size_t size;
    unsigned char *bytes = read_bytes(source_path ? source_path : default_source, &size);
    yyjson_mut_doc *doc = needed(format_json_parse((char *)bytes, size, error, sizeof(error)));
    source = yyjson_mut_doc_get_root(doc);
    if (!yyjson_mut_is_obj(source) || yyjson_mut_obj_size(source) != 2
        || !yyjson_mut_is_obj(yyjson_mut_obj_get(source, "DEFS"))
        || !yyjson_mut_is_obj(yyjson_mut_obj_get(source, "ROOTS")))
        fail("invalid schema source");
    vectors();
    opcodes();
    schemas();
    yyjson_mut_doc_free(doc);
    free(bytes);
    free(default_source);
    printf("%s %u generated schema/vector files (zlib %s).\n",
           writing ? "Wrote" : "Verified",
           count,
           zlibVersion());
    free_fs_arguments(argc, argv);
    return count == 26 ? EXIT_SUCCESS : EXIT_FAILURE;
}
