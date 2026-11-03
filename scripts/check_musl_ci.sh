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
        -o -name '*.goat' -o -name 'host-*.c' -o -name 'edges.c' -o -name 'driver.c' \) -print0 2>/dev/null)
    # Run once, only after all normal work has ended (or failed). Keep this
    # report separate from the gate: no replay result can erase its status.
    if [ "$(uname -m)" = aarch64 ] \
        && [ -x /work/debug/goat ] && [ -f /work/debug/libcommon.a ] \
        && [ -x /work/musl_compile_observer ]; then
        local replay_status=0
        bash /work/source/scripts/report_saturation_compiler.sh \
            /work/debug/goat /work/source \
            /results/compiler-pass-replay /work/musl_compile_observer || replay_status=$?
        printf 'Original musl job status=%s; isolated compiler-report replay status=%s\n' \
            "$status" "$replay_status"
    fi
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
run_observed_ctest debug ctest --test-dir /work/debug --output-on-failure -L "language-roundtrip|unicode-interactive|standalone-bytecode|module-sources-files-migration|imports-include-migration|module-search-migration|module-dependencies-migration|module-graph-migration|module-snapshot-migration|module-identity-serialization-migration|module-initialization-migration|module-contract-exceptions-migration|module-scopes-prototypes-migration|module-access-migration|module-multifile-access-migration|module-context-provenance-migration|module-assignment-migration|module-execution-interface-migration|module-source-diagnostics-migration|module-information-migration|module-dispatch-consistency-migration|module-validation-migration|module-writer-migration|module-build-library-migration|module-loading-migration|module-cli-migration|module-inspect-migration|inspect-probe-migration|module-merge-migration|child-process-migration|cleanup-service-migration|process-exit-migration|module-exit-migration|module-threads-migration|native-requirements-migration|native-toolchains-migration|native-artifact-migration|native-integrity-migration|native-cleanup-migration|native-exit-values-migration|native-runner-migration|native-access-libm-migration|native-strings-migration|native-saturation-migration"
# Includes real default-worker native calls and explicit small-stack fallback.
bash scripts/check_native_pipeline.sh /work/debug/goat /work/debug/unit_testing /work/native-pipeline
ctest --test-dir /work/debug --output-on-failure -R '^integration.module_native$'
python3 scripts/check_module_delivery.py /work/debug/goat /work/debug/goat-inspect gcc
python3 scripts/check_module_book.py /work/debug/goat
python3 scripts/check_module_musl.py produce /work/debug/goat /work/debug/module_native_testing musl /results
python3 scripts/check_module_musl.py verify /work/debug/goat /work/debug/module_native_testing musl /results
BUILD_DIR=/work/release bash scripts/build_release.sh
python3 scripts/check_package.py /work/release/packages
cmake --build /work/release --parallel 2 --target module_native_testing loop_index_testing unicode_interactive_testing bytecode_v1_testing module_sources_scenarios module_files_scenarios module_imports_scenarios source_include_scenarios module_search_scenarios module_dependencies_scenarios module_graph_scenarios module_snapshot_scenarios module_identity_scenarios module_serialization_scenarios module_initialization_scenarios module_initialization_contract_scenarios module_exception_scenarios module_incomplete_scopes_scenarios module_failed_prototypes_scenarios module_access_scenarios module_multifile_access_scenarios module_context_scenarios module_function_provenance_scenarios module_assignment_scenarios module_import_execution_scenarios module_interface_scenarios module_source_diagnostics_scenarios module_information_scenarios module_dispatch_consistency_scenarios module_validation_scenarios module_writer_scenarios module_build_scenarios module_library_scenarios module_loading_scenarios module_cli_scenarios module_inspect_scenarios inspect_probe_scenarios module_merge_scenarios child_process_scenarios cleanup_service_scenarios process_exit_scenarios module_exit_scenarios module_threads_scenarios native_requirements_scenarios native_toolchains_scenarios native_artifact_scenarios native_integrity_scenarios native_cleanup_scenarios native_exit_scenarios module_native_values_scenarios native_runner_testing native_runner_policy_testing native_access_scenarios native_libm_scenarios native_runtime_strings_scenarios native_string_method_scenarios native_saturation_scenarios
run_observed_ctest release ctest --test-dir /work/release --output-on-failure -L "language-roundtrip|unicode-interactive|standalone-bytecode|module-sources-files-migration|imports-include-migration|module-search-migration|module-dependencies-migration|module-graph-migration|module-snapshot-migration|module-identity-serialization-migration|module-initialization-migration|module-contract-exceptions-migration|module-scopes-prototypes-migration|module-access-migration|module-multifile-access-migration|module-context-provenance-migration|module-assignment-migration|module-execution-interface-migration|module-source-diagnostics-migration|module-information-migration|module-dispatch-consistency-migration|module-validation-migration|module-writer-migration|module-build-library-migration|module-loading-migration|module-cli-migration|module-inspect-migration|inspect-probe-migration|module-merge-migration|child-process-migration|cleanup-service-migration|process-exit-migration|module-exit-migration|module-threads-migration|native-requirements-migration|native-toolchains-migration|native-artifact-migration|native-integrity-migration|native-cleanup-migration|native-exit-values-migration|native-runner-migration|native-access-libm-migration|native-strings-migration|native-saturation-migration"
# Release executes the same Debug-produced bytecode/SO, without recompilation.
python3 scripts/check_module_musl.py verify /work/release/goat /work/release/module_native_testing musl /results
cp /work/release/packages/*.tar.gz /results/
