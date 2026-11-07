# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
# Observe the invoking shell and its descendants without changing their handles.
param(
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [int]$IntervalSeconds = 10,
    [int]$MaxSeconds = 840
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$stopFile = Join-Path $OutputDirectory 'stop'
$log = Join-Path $OutputDirectory 'processes.jsonl'
$errors = Join-Path $OutputDirectory 'observer-errors.txt'
$root = (Get-CimInstance Win32_Process -Filter "ProcessId=$PID").ParentProcessId
$rootProcess = Get-Process -Id $root
$rootStart = $rootProcess.StartTime.ToUniversalTime()
$clock = [Diagnostics.Stopwatch]::StartNew()
try {
    do {
        if (Test-Path $stopFile) { break }
        $currentRoot = Get-Process -Id $root -ErrorAction SilentlyContinue
        if (!$currentRoot -or $currentRoot.StartTime.ToUniversalTime() -ne $rootStart) { break }
        try {
            $all = @(Get-CimInstance Win32_Process)
            $ids = @([uint32]$root)
            do {
                $children = @($all | Where-Object {
                    $_.ParentProcessId -in $ids -and $_.ProcessId -notin $ids -and $_.ProcessId -ne $PID
                } | ForEach-Object { $_.ProcessId })
                $ids += $children
            } while ($children.Count -gt 0)
            $rows = @($all | Where-Object { $_.ProcessId -in $ids } | ForEach-Object {
                $p = $_
                $threads = @()
                try {
                    $threads = @((Get-Process -Id $p.ProcessId).Threads | ForEach-Object {
                        $reason = $null
                        if ($_.ThreadState -eq 'Wait') { $reason = $_.WaitReason.ToString() }
                        [ordered]@{ id = $_.Id; state = $_.ThreadState.ToString(); waitReason = $reason }
                    })
                } catch { "$($p.ProcessId): $_" | Add-Content $errors }
                [ordered]@{
                    pid = $p.ProcessId; parent = $p.ParentProcessId; name = $p.Name
                    command = $p.CommandLine; created = $p.CreationDate
                    kernelTime = $p.KernelModeTime; userTime = $p.UserModeTime
                    readBytes = $p.ReadTransferCount; writeBytes = $p.WriteTransferCount
                    handles = $p.HandleCount; threads = $threads
                }
            })
            [ordered]@{ utc = [DateTime]::UtcNow.ToString('o'); root = $root; processes = $rows } |
                ConvertTo-Json -Depth 8 -Compress | Add-Content -Encoding UTF8 $log
        } catch { "Snapshot: $_" | Add-Content $errors }
        # Check the stop marker every second so successful builds exit promptly.
        for ($i = 0; $i -lt $IntervalSeconds -and $clock.Elapsed.TotalSeconds -lt $MaxSeconds; $i++) {
            if (Test-Path $stopFile) { break }
            Start-Sleep -Seconds 1
        }
    } while ($clock.Elapsed.TotalSeconds -lt $MaxSeconds)
} finally {
    [DateTime]::UtcNow.ToString('o') | Set-Content (Join-Path $OutputDirectory 'observer-finished.txt')
}
