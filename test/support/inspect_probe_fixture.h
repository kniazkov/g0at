/** @file inspect_probe_fixture.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent real-compiler archive fixtures for native inspector scenarios.
 */
#pragma once
#include "test/support/execution.h"
#include "yyjson.h"

typedef struct ip_fixture_t ip_fixture_t;

typedef struct {
    const char *name;
    const void *data;
    size_t size;
} ip_part_t;

typedef struct {
    yyjson_mut_doc *doc;
    ip_part_t *parts;
    size_t count, capacity;
} ip_archive_t;

/** All returned storage is owned by the scenario. schema_path is schema-source.json. */
ip_fixture_t *ip_create(scenario_run_t *run,
                        const char *builder,
                        const char *compiler,
                        const char *source_root,
                        const char *schema_path);
ip_archive_t *ip_read(ip_fixture_t *fixture, const char *path);
const char *ip_save(ip_fixture_t *fixture, const char *path, ip_archive_t *archive);
ip_part_t *ip_part(ip_fixture_t *fixture, ip_archive_t *archive, const char *name);
void ip_put(ip_fixture_t *fixture,
            ip_archive_t *archive,
            const char *name,
            const void *data,
            size_t size);
/** Independent reference.normalize equivalent, in place, for merge expectations. */
void ip_normalize(ip_fixture_t *fixture, ip_archive_t *archive);
const char *ip_build(ip_fixture_t *fixture, const char *name);
const char *ip_custom(ip_fixture_t *fixture, const char *original, const char *kind);
void ip_compile(ip_fixture_t *fixture, const char *source, const char *output, bool shared);
