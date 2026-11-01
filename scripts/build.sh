#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
set -euo pipefail

usage() {
    printf 'Usage: %s [jobs]\n' "$0"
    printf 'jobs: positive integer; default: CMAKE_BUILD_PARALLEL_LEVEL or logical CPU count.\n'
}
if [[ $# -eq 1 && ( $1 == --help || $1 == -h ) ]]; then
    usage
    exit 0
fi
if [[ $# -gt 1 ]]; then
    usage >&2
    exit 1
fi
build_jobs="${1:-${CMAKE_BUILD_PARALLEL_LEVEL:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf 1)}}"
if [[ ! $build_jobs =~ ^[1-9][0-9]*$ || ( $# -eq 1 && -z $1 ) ]]; then
    usage >&2
    exit 1
fi

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"
build_dir="${BUILD_DIR:-$repo_root/build}"
mkdir -p "$build_dir"
build_dir="$(cd "$build_dir" && pwd)"

cmake -S "$repo_root" -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER="${CC:-gcc}"
cmake --build "$build_dir" --parallel "$build_jobs" \
    --target process_exit_delayed_testing process_exit_failure_testing process_exit_testing native_artifact_testing native_cleanup_testing module_native_values_testing native_requirements_testing native_toolchain_testing goat goat-inspect goat-merge inspect_probe_testing unit_testing analysis_testing functional_testing module_json_testing child_process_testing child_process_input_testing child_process_stream_testing scenario_context_testing scenario_execution_testing scenario_registry_testing module_schema_generator_testing unicode_generator_testing integer_oracle_testing format_oracle_testing format_binary_testing module_execution_testing inspect_report_testing process_watch_testing loop_index_testing unicode_interactive_testing bytecode_v1_testing module_json_scenarios module_canonical_hash_scenarios cleanup_service_testing zip_scenarios module_container_scenarios module_bytecode_scenarios module_abi_scenarios module_native_scenarios module_sources_scenarios module_files_scenarios module_imports_scenarios source_include_scenarios module_search_scenarios module_dependencies_scenarios module_graph_scenarios module_snapshot_scenarios module_identity_scenarios module_serialization_scenarios module_initialization_scenarios module_initialization_contract_scenarios module_exception_scenarios module_incomplete_scopes_scenarios module_failed_prototypes_scenarios module_access_scenarios module_multifile_access_scenarios module_context_scenarios module_function_provenance_scenarios module_assignment_scenarios module_import_execution_scenarios module_interface_scenarios module_source_diagnostics_scenarios module_information_scenarios module_dispatch_consistency_scenarios module_validation_scenarios module_writer_scenarios module_build_scenarios module_library_scenarios module_loading_scenarios module_cli_scenarios module_inspect_scenarios inspect_probe_scenarios module_merge_scenarios child_process_scenarios cleanup_service_scenarios process_exit_scenarios module_exit_scenarios zip_testing module_container_testing module_bytecode_testing module_native_testing module_abi_testing module_dispatch_testing module_loading_testing module_library_testing module_consistency_testing module_validation_testing module_writer_testing module_writer_fault_testing module_sources_testing module_search_testing module_dependencies_testing module_graph_testing module_snapshot_testing module_identity_testing module_initialization_testing module_serialization_testing module_thread_lifetime_testing module_initialization_errors_testing module_exception_propagation_testing module_initialization_contract_testing module_incomplete_scopes_testing module_failed_prototypes_testing source_include_testing module_import_execution_testing module_imports_testing module_files_testing module_access_testing
cp "$build_dir/goat" "$repo_root/goat"
"$build_dir/module_json_testing"
"$build_dir/unit_testing"
"$build_dir/analysis_testing" "$repo_root/test/data/analysis"
(
    work="$(mktemp -d "$build_dir/functional-fixtures-XXXXXX")"
    cp -R "$repo_root/test/data/functional/." "$work/"
    cd "$work"
    "$build_dir/functional_testing" "$build_dir/goat" list.txt
)
printf '\nDone.\n'
