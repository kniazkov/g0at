# 26. Separate Compilation

[Contents](index.md) · [Русский](../ru/26-separate-compilation.md) · [Previous chapter](25-native-dispatch-and-vm-fallback.md) · [Next chapter](27-observability.md)

Updated in #220 against the implemented baseline [5b98271](https://github.com/kniazkov/g0at/tree/5b98271e26cb17bdc5589aaf827316016ba2e00a). Commit-pinned links to earlier revisions below are historical implementation references; current contracts are linked explicitly.

<a id="section-26-1"></a>

## 26.1. Two saved-program paths

Separate compilation prepares code now and executes it later without the source or a C compiler. The CLI retains two distinct paths:

| Input | Build command | Output |
|---|---|---|
| Standalone `.goat` without include/import | `goat --compile program.goat` | Adjacent standalone `.gbin`, optionally a companion DLL/SO |
| `.goat` using include/import | `goat --compile program.goat` | Adjacent ZIP `.gbin` |
| Named `.gmod` | `goat build app.gmod` or `goat --compile app.gmod` | `modules/<name>/<version>.gbin` ZIP by default |

`build` requires `.gmod`. Direct `.gmod` or `run app.gmod` builds in memory and executes without publishing an archive. `--run` accepts binary input; `run` also accepts sources/descriptions. Format is distinguished by its checked signature, not merely the `.gbin` suffix. The [module CLI reference](../../module-cli-api.md) gives path and option rules.

<a id="section-26-2"></a>

## 26.2. Portable bytes, not host structures

Both paths use the portable v1 bytecode codec: explicit fixed-width little-endian fields, 32-bit Unicode scalars and checked offsets/indices. Serialized bytes contain neither VM pointers nor compiler/analysis objects. The old `GOATBI11` host-dependent images are not accepted; rebuild them from source.

The standalone wrapper in [binary.c](../../../src/vm/binary.c) has signature `GOATPG01`, version 1, a 96-byte header, portable bytecode and optional 16-byte native bindings. The former platform-tag field is zero. Bindings relate `FUNC` instruction indices to validated function IDs. FNV-1a detects accidental wrapper corruption; SHA-256 binds an optional companion library.

A ZIP module is a different container: `module.json`, `bytecode.bin` and declared native files. It carries the canonical main hash, function/file/access/dependency tables and native-variant descriptions. See [format v1](../../module-format-v1/README.md); this ZIP is not the standalone wrapper plus renamed files.

<a id="section-26-3"></a>

## 26.3. Compatibility and identity

Bytecode portability does not make native machine code portable. Native variants carry explicit OS, CPU, bitness, runtime and ABI requirements. `off` can run portable bytecode without loading machine code; `auto` selects a compatible variant or uses VM, while `required` requires a root specialization in a compatible variant.

The module’s main hash excludes native additions and informational metadata, so merging compatible native variants does not change bytecode identity. Dependency entries bind exact names, versions and hashes; a consumer does not override a library’s own dependency versions. All reachable modules are checked before execution and retained as one snapshot. Separate physical copies are not silently combined into a richer variant inventory.

<a id="section-26-4"></a>

## 26.4. Validation before execution

The validator checks the container, strict JSON schema, canonical data, bytecode structure, function/access tables, all native hashes and cross-component consistency. The dependency graph is validated iteratively, with deterministic diagnostics for missing nodes, mismatches and cycles. Unused declared dependencies are warned about without archive lookup. Include cycles between source files are distinct from forbidden module-import cycles.

Ready bytes remain immutable and are held in memory; runtime caches and bindings are separate. Changing or deleting an archive after validation cannot substitute different native bytes during lazy loading.

These checks establish structure and integrity, not author identity or a security sandbox. Hashes are not signatures; an attacker replacing code and its metadata can recompute them. Validated untrusted code is still untrusted code.

<a id="section-26-5"></a>

## 26.5. Atomic modules and standalone companions

Module builds do not execute Goat initializers or load DLL/SO. The writer publishes one deterministic ZIP by atomic replacement after validation; a failed build preserves the previous destination and permissions. `goat-merge` validates inputs and combines native/informational data without loading code or resolving external dependency archives. Conflicting metadata may require a human author-order choice; noninteractive failure leaves output untouched.

The retained standalone path still has a separate `.gbin` and optional same-base `.so`/`.dll`. Their SHA-256 relationship is checked before loading a private snapshot. Publishing two separate files is **not** one atomic transaction: an interrupted replacement may leave a mismatched pair, which is detected rather than repaired automatically. Do not apply this two-file limitation to the one-archive module path.

<a id="section-26-6"></a>

## 26.6. Executing a saved standalone example

From the repository root on Linux:

```sh
mkdir -p build/book-native/run
cp docs/book/examples/22-native-module.goat build/book-native/module.goat
./goat --compile --native required build/book-native/module.goat
cp build/book-native/module.gbin build/book-native/module.so build/book-native/run/
CC=goat-no-such-compiler ./goat --run --native required build/book-native/run/module.gbin
./goat --run --native off build/book-native/run/module.gbin
```

Both executions print `7`, `6`, `4.0` on separate lines. The run directory contains no source, and the nonexistent compiler is never invoked. On Windows use `.\goat.exe`, `Copy-Item`, and the `.dll` companion; save and restore `$env:CC` when testing compiler independence.

For named ZIP-module examples and relocation, follow [chapter 30](30-modules.md) and [delivery tests](../../module-delivery-tests.md). A ZIP module needs no external native companion, but used dependency archives must remain discoverable through the configured module search roots.

<a id="section-26-7"></a>

## 26.7. Failure policy and evidence

| Situation for a ZIP module | `off` | `auto` | `required` |
|---|---|---|---|
| Corrupt archive, native bytes or dependency graph | Failure | Failure | Failure |
| No compatible native root specialization | VM | VM | Failure before user code |
| A call has no applicable specialization | VM | VM | VM |
| Applicable library fails extraction/loading before adapter entry | No load | One warning and VM for that library | Failure |
| Detected ABI violation after adapter entry | No entry | Terminal error; no replay | Terminal error; no replay |

`--run` does not rebuild a missing artifact. Module loading and standalone pure-resource fallback are different contracts, as explained in [chapter 25](25-native-dispatch-and-vm-fallback.md).

Tests include `check_binary_program.sh`, `check_module_cli.py`, `integration.module_graph`, `check_module_loading.py`, and the delivery suite. Linux GCC/Clang x86-64/ARM64 and Windows MINGW32/MINGW64/UCRT64 are covered. Exact old glibc 2.14/2.17 and Windows 10 execution are not established merely by a green newer runner; [delivery evidence](../../module-delivery-tests.md) records tested environments and the remaining lower-bound plan.
