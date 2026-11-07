# Local full checks

[Русский](full-check.ru.md)

Run `bash scripts/build.sh --full-check 4` on Linux or
`scripts\build_mingw.cmd --full-check 4` from Windows cmd/PowerShell. The scripts
work from outside the repository too. The optional number sets both build and
CTest parallelism; without it, the existing jobs/environment/CPU-count default
applies. CTest still honors serial performance cases and resource locks. Use a
small value such as 4 when running alongside other applications.

The existing default, `--build-only` and `--test-only` modes remain unchanged.
`--full-check` is a separate mode: it always configures, builds and tests both
configurations. It does not overwrite the quick build directory. After all checks
succeed, it publishes the Release `goat`, `goat-inspect` and `goat-merge` tools to
`target/` (with `.exe` on Windows). A failed check leaves the previous publication
untouched; a publication failure makes the full check fail.

All build stages, including sanitizer, package, allocation-failure and host-tool
builds, use the same generator-aware output synchronization as the quick scripts.
GNU Make 4.0+ buffers each job with `--output-sync=target`; Ninja keeps its native
buffering without Make flags. Messages may arrive only after a job completes.
Warnings/errors and parallel job counts are preserved.

## Prerequisites

- CMake/CTest 3.21 or newer, Bash, Git and a supported native GCC/Clang toolchain.
  Shell suites also use the ordinary build environment's utilities (including diff).
- Linux: GCC or Clang with the sanitizer libraries and support for
  `-ftrivial-auto-var-init=pattern`, as in CI. An unsupported compiler fails the
  relevant stage; it is not silently accepted as equivalent coverage.
- Windows: use the selected MSYS2 MinGW environment (MINGW32, MINGW64 or UCRT64),
  with its compiler, CMake and make on PATH, and MSYS2 Bash available. A plain
  MSVC environment or WSL Bash is not this runner's target. Delivery scenarios
  also require MINGW64 and UCRT64 GCC. They are discovered through MSYS2 paths;
  alternatively set `GOAT_DELIVERY_MINGW64_CC` and `GOAT_DELIVERY_UCRT64_CC` to
  absolute executable paths. Missing delivery compilers fail before building.
- clang-format **18** (`clang-format-18`), Doxygen and Graphviz enable the optional
  style/documentation/graph tests. Missing tools are shown as disabled by
  CMake/CTest; a successful run without them is not complete tooling coverage.

`CC` selects the compiler (one executable, not shell flags). `FULL_CHECK_DIR`
selects a dedicated build root; relative paths are relative to the repository.
The default is `build/full-check-<host>-<compiler>`, with `MSYSTEM` appended on
Windows when present. Use different roots for different toolchains. For example:

```sh
CC=clang FULL_CHECK_DIR=build/check-clang bash scripts/build.sh --full-check 4
```

In PowerShell, set `$env:CC` / `$env:FULL_CHECK_DIR` before invoking the `.cmd` file.
No packages are installed and no old workspaces are deleted by the driver.

## Coverage

| Phase | Checks |
| --- | --- |
| Debug | All registered CTest suites except Release-only timing/package entries; explicitly builds excluded sanitizer runners on Linux |
| Release | CI's release-safe scenario labels, format/frozen references, native-mode and persisted-program checks, serialized performance test |
| Package | Build archive, reconfigure to enable its test, relocate/run using Release and Debug hosts |
| Allocation failures | Separate Debug and Release builds with `GOAT_TEST_ALLOC_FAILURE=ON` |
| Stack initialization | Linux JSON/hash checks with nonzero automatic-variable initialization |
| Host tools | Standalone Unicode and module-schema configure/build/test, independent of the interpreter build |

Release intentionally does not run every Debug component host: some use `assert`
as part of their test machinery. The label selection matches CI and the driver
regression test detects drift. Native Release functional checks use the Debug
functional runner, just as CI does. Newly registered Debug tests are picked up
without maintaining another executable list.

Not covered: other architectures/compilers, Docker-controlled glibc minimum and
paired glibc/musl execution, GitHub artifact upload/retention and pristine-checkout
checks (a local working tree may contain your edits). Ordinary libc controls still
run; their real-pair cases explicitly skip without the externally supplied artifacts.
This command approximates one platform's CI, not the whole matrix.

## Results and failures

The driver prints each stage and writes `<build-root>/summary.txt`. CTest output
is retained as `<configuration>/<stage>.log`, with standard `Testing/Temporary`
logs and scenario-owned failure workspaces alongside it. Logs are replaced on the
next invocation; copy them first if comparing runs.

Independent stages continue after a failure, but tests are not run for a build
that failed. Any failed command makes the final exit code nonzero. Disabled/skipped
tests remain visible in CTest logs and must be reviewed separately from PASS stages.
The driver does not retry failing tests or hide cleanup failures; the known Windows
module-delivery cleanup issue can therefore reproduce locally as well.

Developer regression controls: `cmake -P test/full_check_driver.cmake`. They run the
real driver against a tiny fixture with paths containing spaces, verify Release
selection/package registration and failure propagation, and ensure later independent
stages still run. CI executes these controls on Linux and Windows; they do not
recursively run the full Goat suite.
