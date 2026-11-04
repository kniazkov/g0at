#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Run inside the native-architecture Alpine CI container, not an emulator.
set -euo pipefail
# This observer samples resources only. It never traces, signals, or changes
# any compiler/test process and does not alter CTest arguments or deadlines.
observer_pid=""
stop_compile_observer() {
    local status=0
    if [ -n "$observer_pid" ]; then
        kill -TERM "$observer_pid" 2>/dev/null || true
        wait "$observer_pid" || status=$?
        observer_pid=""
    fi
    return "$status"
}
run_observed_ctest() {
    local config="$1" status=0 observer_status=0
    shift
    /work/musl_compile_observer "/results/compile-resources-$config.log" &
    observer_pid=$!
    printf 'Compiler observation: %s CTest start wall=%s\n' "$config" "$(date +%s)"
    "$@" || status=$?
    printf 'Compiler observation: %s CTest end wall=%s status=%s\n' "$config" "$(date +%s)" "$status"
    stop_compile_observer || observer_status=$?
    if [ "$observer_status" -ne 0 ]; then
        printf 'Compiler resource observer failed with status %s\n' "$observer_status" >&2
        [ "$status" -ne 0 ] || status="$observer_status"
    fi
    return "$status"
}
# Keep the migrated native runner's nested diagnostics even when Docker exits early.
finish_native_diagnostics() {
    local status=$? observer_status=0
    trap - EXIT
    set +e
    stop_compile_observer || observer_status=$?
    if [ "$observer_status" -ne 0 ]; then
        printf 'Compiler resource observer failed during exit with status %s\n' "$observer_status" >&2
        [ "$status" -ne 0 ] || status="$observer_status"
    fi
    # Package runs stay outside the checkout; the C runner copies retained
    # work here before exiting. Preserve the whole tree, not a suffix whitelist.
    local config
    for config in debug release; do
        if [ -d "/work/$config/package-diagnostics" ]; then
            mkdir -p "/results/package-diagnostics/$config"
            cp -a "/work/$config/package-diagnostics/." "/results/package-diagnostics/$config/" || {
                printf 'Failed to preserve package diagnostics for %s\n' "$config" >&2
                [ "$status" -ne 0 ] || status=1
            }
        fi
    done
    # Book scenarios retain their complete copied tutorial, including archives,
    # descriptors and extracted README snippets; never whitelist suffixes here.
    local tutorial destination
    for config in debug release; do
        for tutorial in "/work/$config"/scenario-*/"module tutorial"; do
            [ -d "$tutorial" ] || continue
            destination="/results/module-book-diagnostics/${tutorial#/work/}"
            mkdir -p "$destination"
            cp -a "$tutorial/." "$destination/" || {
                printf 'Failed to preserve book diagnostics for %s\n' "$tutorial" >&2
                [ "$status" -ne 0 ] || status=1
            }
        done
    done
    while IFS= read -r -d '' file; do
        local destination="/results/native-runner-diagnostics/${file#/work/}"
        mkdir -p "$(dirname "$destination")"
        cp "$file" "$destination"
        if [ "$status" -ne 0 ] && [[ "$file" == */execution.log || "$file" == *.stderr ]]; then
            printf '\n--- %s ---\n' "$file"
            tail -c 65536 "$file"
        fi
    done < <(find /work/debug/native-functional /work/release/native-functional \
        /work/debug/ctest/native-original /work/release/ctest/native-original \
        /work/debug/native-original-* /work/release/native-original-* \
        /work/debug/scenario-* /work/release/scenario-* \
        -type f \( -name '*.log' -o -name '*.stdout' -o -name '*.stderr' \
        -o -name '*.report' -o -name '*.command.json' -o -name 'case.name' \
        -o -name '*.goat' -o -name '*.gbin' -o -name '*.gmod' -o -name '*.txt' -o -name 'host-*.c' -o -name 'edges.c' -o -name 'driver.c' \
        \) -print0 2>/dev/null)
    exit "$status"
}
trap finish_native_diagnostics EXIT
mkdir -p /work/source
# Avoid copying the host's .git or glibc build tree into the musl build.
for item in CMakeLists.txt .github lib src scripts docs test tools example include third_party README.md README.ru.md LICENSE.txt; do
    cp -a "/source/$item" /work/source/
done
cd /work/source
export CC=gcc CXX=/nonexistent/cxx CMAKE_BUILD_PARALLEL_LEVEL=2
mkdir -p /results
if [ "$(uname -m)" = aarch64 ]; then
    # Metadata only: these queries do not compile another program or change
    # the selected compiler, its environment, or any existing test command.
    {
        uname -a
        apk info -v gcc musl musl-dev
        gcc -v
        compiler_frontend="$(gcc -print-prog-name=cc1)"
        sha256sum /usr/bin/gcc "$compiler_frontend" /lib/ld-musl-aarch64.so.1
    } > /results/compiler-identity.log 2>&1
fi
gcc -std=c11 -O2 -Wall -Wextra -Werror test/support/musl_compile_observer.c -o /work/musl_compile_observer
BUILD_DIR=/work/debug bash scripts/build.sh 2
run_observed_ctest debug ctest --test-dir /work/debug --output-on-failure -L "language-roundtrip|unicode-interactive|standalone-bytecode|module-sources-files-migration|imports-include-migration|module-search-migration|module-dependencies-migration|module-graph-migration|module-snapshot-migration|module-identity-serialization-migration|module-initialization-migration|module-contract-exceptions-migration|module-scopes-prototypes-migration|module-access-migration|module-multifile-access-migration|module-context-provenance-migration|module-assignment-migration|module-execution-interface-migration|module-source-diagnostics-migration|module-information-migration|module-dispatch-consistency-migration|module-validation-migration|module-writer-migration|module-build-library-migration|module-loading-migration|module-cli-migration|module-inspect-migration|inspect-probe-migration|module-merge-migration|module-delivery-migration|package-controls|module-book-migration|child-process-migration|cleanup-service-migration|process-exit-migration|module-exit-migration|module-threads-migration|native-requirements-migration|native-toolchains-migration|native-artifact-migration|native-integrity-migration|native-cleanup-migration|native-exit-values-migration|native-runner-migration|native-access-libm-migration|native-strings-migration|native-saturation-migration|native-performance-controls|module-musl-migration"
# Includes real default-worker native calls and explicit small-stack fallback.
bash scripts/check_native_pipeline.sh /work/debug/goat /work/debug/unit_testing /work/native-pipeline
ctest --test-dir /work/debug --output-on-failure -R '^integration.module_native$'
/work/debug/module_delivery_scenarios integration.module_delivery.end-to-end \
    /work/debug/goat /work/debug/goat-inspect /work/debug/goat-merge gcc \
    /work/debug /work/source/test/data /work/source/docs/module-format-v1
ctest --test-dir /work/debug --output-on-failure -R '^integration[.]module_book$'
(cd /results && sha256sum -c glibc.sha256)
/work/debug/module_musl_scenarios integration.module_musl.produce /work/debug/goat /work/debug/goat-inspect /work/debug/goat-merge /work/debug/module_native_testing gcc musl /work/debug test/data docs/module-format-v1 --destination /results
(cd /results && sha256sum musl.gbin musl-host.json > musl.sha256)
(cd /results && sha256sum -c glibc.sha256 musl.sha256)
/work/debug/module_musl_scenarios integration.module_musl.verify /work/debug/goat /work/debug/goat-inspect /work/debug/goat-merge /work/debug/module_native_testing gcc musl /work/debug test/data docs/module-format-v1 --destination /results
(cd /results && sha256sum -c glibc.sha256 musl.sha256)
BUILD_DIR=/work/release bash scripts/build_release.sh
for config in /work/debug /work/release; do
    "$config/package_scenarios" integration.package.end-to-end /work/release/packages "$config/module_delivery_scenarios" "$(command -v "${CC:-gcc}")" "$PWD" "$(command -v cmake)" --timeout-seconds 1800
done
cmake --build /work/release --parallel 2 --target module_musl_scenarios module_native_testing loop_index_testing unicode_interactive_testing bytecode_v1_testing module_sources_scenarios module_files_scenarios module_imports_scenarios source_include_scenarios module_search_scenarios module_dependencies_scenarios module_graph_scenarios module_snapshot_scenarios module_identity_scenarios module_serialization_scenarios module_initialization_scenarios module_initialization_contract_scenarios module_exception_scenarios module_incomplete_scopes_scenarios module_failed_prototypes_scenarios module_access_scenarios module_multifile_access_scenarios module_context_scenarios module_function_provenance_scenarios module_assignment_scenarios module_import_execution_scenarios module_interface_scenarios module_source_diagnostics_scenarios module_information_scenarios module_dispatch_consistency_scenarios module_validation_scenarios module_writer_scenarios module_build_scenarios module_library_scenarios module_loading_scenarios module_cli_scenarios module_inspect_scenarios inspect_probe_scenarios module_merge_scenarios module_delivery_scenarios package_scenarios module_book_scenarios child_process_scenarios cleanup_service_scenarios process_exit_scenarios module_exit_scenarios module_threads_scenarios native_requirements_scenarios native_toolchains_scenarios native_artifact_scenarios native_integrity_scenarios native_cleanup_scenarios native_exit_scenarios module_native_values_scenarios native_runner_testing native_runner_policy_testing native_access_scenarios native_libm_scenarios native_runtime_strings_scenarios native_string_method_scenarios native_saturation_scenarios native_performance_scenarios timed_execution_testing
run_observed_ctest release ctest --test-dir /work/release --output-on-failure -L "language-roundtrip|unicode-interactive|standalone-bytecode|module-sources-files-migration|imports-include-migration|module-search-migration|module-dependencies-migration|module-graph-migration|module-snapshot-migration|module-identity-serialization-migration|module-initialization-migration|module-contract-exceptions-migration|module-scopes-prototypes-migration|module-access-migration|module-multifile-access-migration|module-context-provenance-migration|module-assignment-migration|module-execution-interface-migration|module-source-diagnostics-migration|module-information-migration|module-dispatch-consistency-migration|module-validation-migration|module-writer-migration|module-build-library-migration|module-loading-migration|module-cli-migration|module-inspect-migration|inspect-probe-migration|module-merge-migration|module-delivery-migration|package-controls|module-book-migration|child-process-migration|cleanup-service-migration|process-exit-migration|module-exit-migration|module-threads-migration|native-requirements-migration|native-toolchains-migration|native-artifact-migration|native-integrity-migration|native-cleanup-migration|native-exit-values-migration|native-runner-migration|native-access-libm-migration|native-strings-migration|native-saturation-migration|native-performance-controls|module-musl-migration"
# Release executes the same Debug-produced bytecode/SO, without recompilation.
(cd /results && sha256sum -c glibc.sha256 musl.sha256)
/work/release/module_musl_scenarios integration.module_musl.verify /work/release/goat /work/release/goat-inspect /work/release/goat-merge /work/release/module_native_testing gcc musl /work/release test/data docs/module-format-v1 --destination /results
(cd /results && sha256sum -c glibc.sha256 musl.sha256)
cp /work/release/packages/*.tar.gz /results/
