# 29. Performance Measurement

[Contents](index.md) · [Русский](../ru/29-performance-measurement.md) · [Previous](28-testing.md) · [Next](appendix-a-terminology.md)

Revision 3. Implementation described: [commit cf51b8c, including `for`](https://github.com/kniazkov/g0at/tree/cf51b8cb27a102d15260c7822ce503404462b0fd).

<a id="section-29-1"></a>

## 29.1. The experiment's question

[check_native_performance.py](https://github.com/kniazkov/g0at/blob/cf51b8cb27a102d15260c7822ce503404462b0fd/scripts/check_native_performance.py) compares two executions of one workload: saved bytecode in the VM and a saved program with a native library. It tests a narrow claim: for this numeric call tree, median native-process time must be lower than median VM-process time.

This is not a language ranking or an estimate for an arbitrary application. The workload deliberately contains many calls to pure numeric functions, the area served by the current backend. It does not represent input, strings, objects, or exceptions.

<a id="section-29-2"></a>

## 29.2. The call tree

The generator creates `leaf(n)`, computing `n * n + 3 * n + 7`, and functions `level1` through `level11`. Each level calls its predecessor twice, with `n + 1` and `n + 2`. Top-level code sums 1024 trees with starting arguments from 0 through 1023.

At depth 12, one tree contains \(2^{12}-1=4095\) function entries, for 4,193,280 entries overall. The longest chain is shallow, but the call count is large. The generator writes out top-level calls individually, requiring no Goat loops.

The native adapter's budget is 4096 entries, so one tree fits; the next top-level call starts a new budget. The report should show 1024 adapter entries, not millions of internal C calls.

Python computes the expected sum independently through binomial coefficients: over 11 levels, the number of paths with a given count of `2` increments is known combinatorially. This checks the answer by a different method from recursive Goat execution.

<a id="section-29-3"></a>

## 29.3. Preparation is excluded from the samples

The script creates identical `vm.goat` and `native.goat`, then invokes `--compile --native off` and `--compile --native required`. Both commands must finish without stdout or stderr. Both sources are removed after preparation; an inspection copy is retained as `call_tree.goat`.

Before execution, `CC` becomes `goat-no-such-compiler`. Only `.gbin` files are run. Hidden compilation during measurement therefore cannot be part of a successful scenario. Preparation and separate warm-ups are technically timed by the common helper, but their elapsed times are not added to the samples.

<a id="section-29-4"></a>

## 29.4. What is measured

`time.perf_counter_ns()` records timestamps before and after `subprocess.run`. This is elapsed process time, not just the duration of arithmetic instructions. It includes interpreter startup, `.gbin` reading and validation, snapshot-library loading in the native variant, execution, output, report writing, and shutdown.

One warm-up for each variant precedes five measurement pairs. Pair order alternates: VM/native first, then native/VM. This reduces systematic order bias without eliminating scheduling, CPU-frequency, or filesystem-cache effects.

Every run must produce exact stdout and no stderr. Its report must confirm:

| Field | VM | Native variant |
|---|---|---|
| `preparation` | `disabled` | `ready` |
| `attempts` | 0 | 1024 |
| `succeeded` | 0 | 1024 |
| `retries` | 0 | 0 |

Without this check, a fast run using an unintended mode or producing a wrong answer would be meaningless. Each child process has a 180-second timeout.

<a id="section-29-5"></a>

## 29.5. Commands and results

Use a Release build without `MEMORY_DEBUG`. From the repository root on Linux:

```sh
bash scripts/build_release.sh
python3 scripts/check_native_performance.py ./goat build/native-performance
```

In PowerShell, with Python Launcher available:

```powershell
.\scripts\build_release_mingw.cmd
py -3 .\scripts\check_native_performance.py .\goat.exe .\build\native-performance
```

The second positional argument is the working directory; `python` may replace `py -3` if it selects the intended Python 3. Building the native half requires a C compiler available through `CC` or its default name.

`timings.json` holds tree parameters, expected result, every `samples_ns` measurement, `vm_median_ns` and `native_median_ns`, the `speedup` ratio, and `passed`. The median is the middle element of the sorted sample: here, the third of five. Passing requires strictly `native_median_ns < vm_median_ns`.

<a id="section-29-6"></a>

## 29.6. Citing a measurement

A published result should state commit, OS and architecture, CPU, compiler and version, build settings, command, raw samples, and both medians. A ratio without those details cannot be meaningfully reproduced. `*.report` files confirm execution mode, while `timings.json` permits checking the ratio calculation.

> [!CAUTION]
> The script saves timings and workload parameters, not a complete machine and build inventory. Five measurements and alternating order provide no statistical guarantee; background load can fail the speed check despite a correct answer. The script requires an actual speedup and does not downgrade failure to a warning.

Compilation time is deliberately excluded, while loading overhead is included. The result therefore concerns rerunning a prepared program. It does not show whether compilation pays off for one short call or promise the same ratio for another program. That is the boundary of the experiment's claim.
