#!/usr/bin/env bash
# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
set -euo pipefail
source scripts/windows_shell_pid.sh
root_pid=$(windows_shell_pid)
output=build/ci/observer-control
mkdir -p "$output"
rm -f "$output/stop" "$output/child.pid" "$output/processes.jsonl"
export GOAT_OBSERVER_CONTROL_DIR
GOAT_OBSERVER_CONTROL_DIR=$(cygpath -w "$PWD/$output")
# A native child launched via the same MSYS2 background path as production.
powershell.exe -NoProfile -Command '
    $PID | Set-Content (Join-Path $env:GOAT_OBSERVER_CONTROL_DIR "child.pid")
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while (!(Test-Path (Join-Path $env:GOAT_OBSERVER_CONTROL_DIR "stop")) -and $clock.Elapsed.TotalSeconds -lt 40) {
        Start-Sleep -Milliseconds 100
    }
' > "$output/child-console.txt" 2>&1 &
child=$!
cleanup() {
    result=$?
    trap - EXIT
    touch "$output/stop"
    wait "$child" || true
    exit "$result"
}
trap cleanup EXIT
for ((i=0; i<100; i++)); do
    [[ -s "$output/child.pid" ]] && break
    sleep 0.1
 done
[[ -s "$output/child.pid" ]]
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/observe_windows_build.ps1 \
    -RootProcessId "$root_pid" -OutputDirectory "$GOAT_OBSERVER_CONTROL_DIR" -IntervalSeconds 1 -MaxSeconds 3
powershell.exe -NoProfile -Command '
    $ErrorActionPreference = "Stop"
    $dir = $env:GOAT_OBSERVER_CONTROL_DIR
    $childId = [int](Get-Content (Join-Path $dir "child.pid"))
    $records = @(Get-Content (Join-Path $dir "processes.jsonl") | ForEach-Object { $_ | ConvertFrom-Json })
    $found = @($records | ForEach-Object { $_.processes } | Where-Object { $_.pid -eq $childId })
    if ($found.Count -eq 0) { throw "Observer missed native child PID $childId" }
    if (!(Test-Path (Join-Path $dir "observer-finished.txt"))) { throw "Observer did not finish" }
'
before=$(wc -l < "$output/processes.jsonl")
touch "$output/stop"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/observe_windows_build.ps1 \
    -RootProcessId "$root_pid" -OutputDirectory "$GOAT_OBSERVER_CONTROL_DIR" -MaxSeconds 3
[[ $(wc -l < "$output/processes.jsonl") == "$before" ]]
