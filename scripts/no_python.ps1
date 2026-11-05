# Copyright 2026 Ivan Kniazkov
# Distributed under the MIT license; see LICENSE.txt.
# Keep the standalone Windows generators on the original PowerShell/MSVC path.
# The MinGW compiler is used only to prepare the native poison executable.
param([Parameter(Mandatory = $true)][scriptblock]$Command)
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = Split-Path $PSScriptRoot -Parent
$id = [Guid]::NewGuid().ToString('N')
$bin = Join-Path $env:RUNNER_TEMP "goat-no-python-$id"
$evidence = Join-Path $root "build/python-free/pwsh-$id"
$originalPath = $env:PATH
$originalLog = $env:GOAT_NO_PYTHON_LOG
$failure = $null
$projectStarted = $false
$projectStatus = 0
New-Item -ItemType Directory -Force $bin, $evidence | Out-Null
$attempts = Join-Path $evidence 'attempts.log'
[IO.File]::WriteAllText($attempts, '')
function Assert-NoPythonAttempt([string]$path) {
    if ((Get-Item $path).Length -ne 0) {
        Get-Content $path | Write-Host
        throw 'Python-free guard: forbidden interpreter attempt, even if its error was ignored.'
    }
}
try {
    if (-not $env:GOAT_NO_PYTHON_CC) { throw 'Missing native guard preparation compiler' }
    $env:PATH = "$(Split-Path $env:GOAT_NO_PYTHON_CC);$originalPath"
    & $env:GOAT_NO_PYTHON_CC -std=c11 -Wall -Wextra -Werror "$root/test/support/python_forbidden.c" -o "$bin/forbidden.exe"
    if ($LASTEXITCODE -ne 0) { throw 'Cannot compile native poison executable' }
    $env:PATH = $originalPath
    $names = @('python', 'python2', 'python3', 'pythonw', 'pythonw2', 'pythonw3', 'py', 'pyw', 'pypy', 'pypy2', 'pypy3')
    foreach ($major in 2, 3) {
        foreach ($minor in 0..30) {
            $names += "python$major.$minor", "pythonw$major.$minor", "pypy$major.$minor"
        }
    }
    foreach ($directory in $originalPath.Split(';')) {
        if (-not $directory -or -not (Test-Path -LiteralPath $directory -PathType Container)) { continue }
        foreach ($file in Get-ChildItem -LiteralPath $directory -File) {
            if ($file.Name -match '^(pythonw?|pypyw?)([0-9]+([.][0-9]+)*)?([.]exe)?$|^pyw?([.]exe)?$') {
                $names += $file.BaseName
                $file.FullName | Out-File "$evidence/host-path-inventory.log" -Append
            }
        }
    }
    foreach ($name in ($names | Sort-Object -Unique)) {
        Copy-Item "$bin/forbidden.exe" "$bin/$name.exe"
    }
    $env:PATH = "$bin;$originalPath"
    $control = Join-Path $evidence 'negative-controls.log'
    $env:GOAT_NO_PYTHON_LOG = $control
    foreach ($name in 'python', 'python3', 'python3.13', 'pythonw', 'py', 'pyw', 'pypy3') {
        & $name --version 2>> "$evidence/negative-controls.stderr"
        if ($LASTEXITCODE -ne 99) { throw "Poison alias failed: $name" }
    }
    & "$bin/python.exe" --version 2>> "$evidence/negative-controls.stderr"
    if ($LASTEXITCODE -ne 99) { throw 'Absolute poison control failed' }
    & cmd.exe /d /c 'python.exe --version' 2>> "$evidence/negative-controls.stderr"
    if ($LASTEXITCODE -ne 99) { throw 'Native cmd poison control failed' }
    # Deliberately ignore the native result, then test the final-log assertion.
    & py.exe --version 2>> "$evidence/negative-controls.stderr"
    $detected = $false
    try { Assert-NoPythonAttempt $control } catch { $detected = $true; $_ | Out-File "$evidence/ignored-error-control.log" }
    if (-not $detected) { throw 'Ignored-error detection control failed' }
    Remove-Item "$bin/invoked"
    $env:GOAT_NO_PYTHON_LOG = $null
    & "$bin/python.exe" --version 2>> "$evidence/negative-controls.stderr"
    if ($LASTEXITCODE -ne 98 -or -not (Test-Path "$bin/invoked")) { throw 'Missing-log control failed' }
    Move-Item "$bin/invoked" "$evidence/missing-log-control.marker"
    $env:GOAT_NO_PYTHON_LOG = $attempts
    & cmake "-DSOURCE=$root" -P "$PSScriptRoot/check_no_python.cmake" 2>&1 | Tee-Object "$evidence/audit-source.log"
    if ($LASTEXITCODE -ne 0) { throw 'Project source audit failed' }
    $global:LASTEXITCODE = 0
    $PSNativeCommandUseErrorActionPreference = $true
    $projectStarted = $true
    & $Command
    if ($LASTEXITCODE -ne 0) { throw "Project command failed: $LASTEXITCODE" }
} catch {
    $failure = $_
    if ($projectStarted -and $LASTEXITCODE -ne 0) { $projectStatus = $LASTEXITCODE }
} finally {
    $PSNativeCommandUseErrorActionPreference = $false
    try {
        & cmake "-DSOURCE=$root" -P "$PSScriptRoot/check_no_python.cmake" 2>&1 | Tee-Object "$evidence/audit-final.log"
        if ($LASTEXITCODE -ne 0) { throw 'Generated CTest audit failed' }
        Assert-NoPythonAttempt $attempts
        if ($env:GOAT_NO_PYTHON_LOG -ne $attempts) { throw 'Python-free guard log environment was changed' }
        if (Test-Path "$bin/invoked") {
            Copy-Item "$bin/invoked" "$evidence/invoked"
            throw 'Python-free native marker detected a forbidden invocation'
        }
    } catch { if (-not $failure) { $failure = $_ } }
    $env:PATH = $originalPath
    $env:GOAT_NO_PYTHON_LOG = $originalLog
    # Preserve every log before deleting only the generated poison aliases.
    if ($failure) { $failure | Out-File "$evidence/result.log" }
    else { 'Project step exit status: 0' | Out-File "$evidence/result.log" }
    try { Remove-Item -LiteralPath $bin -Recurse -Force }
    catch { if (-not $failure) { $failure = $_ } }
}
if ($failure) {
    Write-Host $failure
    if ($projectStatus -ne 0) { exit $projectStatus }
    exit 1
}
