/** @file module_build.h
 * @copyright 2026 Ivan Kniazkov
 * @brief Transactional module compilation with one optional native variant.
 */
#pragma once
#include "cli/options.h"
#include "codegen/native_compiler.h"
#include "codegen/native_toolchain.h"
#include "module_compilation.h"
#include "vm/module_writer.h"

/** CLI overrides are optional; absent values come from .gmod, then auto/host.
 * One context should be shared with all native consumers for an owner run.
 * Neither compilation nor discovery loads a native library or executes Goat code.
 */
typedef struct {
    bool override_mode;
    native_execution_mode_t mode;
    const char *target;
    const char *compiler;
    uint32_t probe_timeout_seconds;
    native_build_context_t *context;
    native_toolchain_context_t *toolchains;
} module_build_options_t;

typedef struct {
    module_compilation_t *compilation;
    void *native_bytes;
    size_t native_size;
    char *native_name;
    char *warning;
} module_build_t;

/** No publication on failure. Sources and graph are frozen, borrowed inputs. */
bool build_module(const module_sources_t *sources,
                  const module_dependencies_t *dependencies,
                  const module_graph_t *graph,
                  const module_build_options_t *options,
                  module_build_t **out,
                  module_compilation_error_t *error);
void free_module_build(module_build_t *build);
/** Borrowed writer inputs; native points to caller storage for the optional record. */
module_writer_input_t module_build_writer_input(const module_build_t *build,
                                                module_writer_native_t *native);
