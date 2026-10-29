#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Run inside the native-architecture Alpine CI container, not an emulator.
set -euo pipefail
mkdir -p /work/source
# Avoid copying the host's .git or glibc build tree into the musl build.
for item in CMakeLists.txt .github lib src scripts docs test tools example include third_party README.md README.ru.md LICENSE.txt; do
    cp -a "/source/$item" /work/source/
done
cd /work/source
export CC=gcc CXX=/nonexistent/cxx CMAKE_BUILD_PARALLEL_LEVEL=2
BUILD_DIR=/work/debug bash scripts/build.sh 2
ctest --test-dir /work/debug --output-on-failure -L "language-roundtrip|unicode-interactive|standalone-bytecode|module-sources-files-migration|imports-include-migration|module-search-migration|module-dependencies-migration|module-graph-migration|module-snapshot-migration|module-identity-serialization-migration|module-initialization-migration|module-contract-exceptions-migration|module-scopes-prototypes-migration|module-access-migration|module-multifile-access-migration|module-context-provenance-migration"
# Includes real default-worker native calls and explicit small-stack fallback.
bash scripts/check_native_pipeline.sh /work/debug/goat /work/debug/unit_testing /work/native-pipeline
python3 scripts/check_native_requirements.py /work/debug/native_requirements_testing
ctest --test-dir /work/debug --output-on-failure -R '^integration.module_native$'
python3 scripts/check_native_toolchains.py /work/debug/native_toolchain_testing gcc
python3 scripts/check_module_build.py /work/debug/module_import_execution_testing gcc
python3 scripts/check_module_library.py /work/debug/module_import_execution_testing /work/debug/module_library_testing gcc
python3 scripts/check_module_loading.py /work/debug/module_loading_testing /work/debug/module_import_execution_testing gcc
python3 scripts/check_module_delivery.py /work/debug/goat /work/debug/goat-inspect gcc
python3 scripts/check_module_book.py /work/debug/goat
python3 scripts/check_module_musl.py produce /work/debug/goat /work/debug/module_native_testing musl /results
python3 scripts/check_module_musl.py verify /work/debug/goat /work/debug/module_native_testing musl /results
BUILD_DIR=/work/release bash scripts/build_release.sh
python3 scripts/check_package.py /work/release/packages
cmake --build /work/release --parallel 2 --target module_native_testing loop_index_testing unicode_interactive_testing bytecode_v1_testing module_sources_scenarios module_files_scenarios module_imports_scenarios source_include_scenarios module_search_scenarios module_dependencies_scenarios module_graph_scenarios module_snapshot_scenarios module_identity_scenarios module_serialization_scenarios module_initialization_scenarios module_initialization_contract_scenarios module_exception_scenarios module_incomplete_scopes_scenarios module_failed_prototypes_scenarios module_access_scenarios module_multifile_access_scenarios module_context_scenarios module_function_provenance_scenarios
ctest --test-dir /work/release --output-on-failure -L "language-roundtrip|unicode-interactive|standalone-bytecode|module-sources-files-migration|imports-include-migration|module-search-migration|module-dependencies-migration|module-graph-migration|module-snapshot-migration|module-identity-serialization-migration|module-initialization-migration|module-contract-exceptions-migration|module-scopes-prototypes-migration|module-access-migration|module-multifile-access-migration|module-context-provenance-migration"
# Release executes the same Debug-produced bytecode/SO, without recompilation.
python3 scripts/check_module_musl.py verify /work/release/goat /work/release/module_native_testing musl /results
cp /work/release/packages/*.tar.gz /results/
