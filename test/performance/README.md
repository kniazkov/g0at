# Precompiled call-tree benchmark

Build Goat in Release mode, then run:

```sh
python3 scripts/check_native_performance.py build/release/goat build/native-performance
```

On Windows, use `python` and `build/release/goat.exe`. Python uses only its standard
library; it is a test dependency, not a dependency of the interpreter.
`CC` selects the native compiler during preparation, as in ordinary compilation.

The script generates twelve pure integer functions. Each non-leaf function calls
the next function twice with different arguments; the leaf computes
`n * n + 3 * n + 7`. The entry function is called 1,024 times with different inputs.
That produces **4,193,280 function calls**, with at most **12 active Goat function
calls**, without a deep recursive stack. Each native entry uses 4,095 calls, below
the existing 4,096-call guard budget; runtime limits are not changed for the test.

Both variants are compiled before measurement: one `.gbin` with native support off,
and one `.gbin` with its native companion. Source inputs are then removed and `CC`
is set to a nonexistent compiler. Timed commands use only `--run`: `--native off`
for the VM and `--native required` for native execution.

Each variant gets an untimed warm-up, followed by five measured runs. The first
pair runs VM then native; subsequent pairs alternate order. A monotonic high-resolution
wall clock measures the complete execution process, including startup, bytecode/library
loading, output, reports and cleanup. Compilation and warm-up are excluded. This is
an end-to-end execution benchmark, not an isolated native-kernel timer.

Every run must return the exact expected integer, computed independently using
binomial coefficients. Native reports must show 1,024 successful adapter entries
and zero retries; VM reports must show zero native attempts. The test then **passes
only when the native median is strictly less than the VM median**. It does not skip
or soften a slower result. Medians reduce transient noise, but any wall-time CI
benchmark can still be affected by host contention.

The output directory retains the generated `call_tree.goat`, both binaries, the
native library, all dispatch reports and `timings.json` with raw nanosecond samples,
medians and speedup. CI runs this benchmark against Release builds on Linux GCC/Clang
and Windows MINGW32/MINGW64/UCRT64, preserving results in the native execution artifacts.
