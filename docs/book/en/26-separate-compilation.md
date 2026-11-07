# 26. Separate Compilation

[Contents](index.md) · [Русский](../ru/26-separate-compilation.md) · [Previous chapter](25-native-dispatch-and-vm-fallback.md) · [Next chapter](27-observability.md)

<a id="section-26-1"></a>
## 26.1. One saved-program format

Every `.gbin` is a ZIP module containing `module.json`, `bytecode.bin` and any declared native variants. `GOATPG01` wrappers and raw bytecode are rejected; rebuild old artifacts from source.

| Input | Command | Default output |
|---|---|---|
| `program.goat` | `goat --compile program.goat` | `program.gbin`, identity `program@0` |
| `app.gmod` | `goat build app.gmod` or `goat --compile app.gmod` | `modules/<name>/<version>.gbin` |

A source basename without its extension must be a valid module name. Invalid names are rejected, not silently rewritten: rename the source or supply a `.gmod` description with an explicit identity. Includes become files inside the module; imports remain exact-version external dependencies. Direct source execution uses the same compiler and validation, passing owned in-memory parts to the loader without encoding and decoding a temporary ZIP. The REPL remains separate.

<a id="section-26-2"></a>
## 26.2. Portable bytes

The portable v1 bytecode codec uses fixed-width little-endian fields, 32-bit characters and checked offsets. Serialized data contains no VM pointers or compiler objects. Metadata carries function/file/access/dependency tables and native variant requirements. See [format v1](../../module-format-v1/README.md).

<a id="section-26-3"></a>
## 26.3. Identity and portability

Native code has explicit OS, architecture, runtime and ABI requirements. The main hash excludes native additions and informational metadata, so merging native variants preserves bytecode identity. Imports bind exact names, versions and hashes. `goat-inspect`, `goat-merge` and import loading accept compiled direct sources just like described modules.

<a id="section-26-4"></a>
## 26.4. Validation before execution

The loader validates strict metadata, bytecode, hashes, native files and the complete reachable dependency graph before execution. In-memory compilation uses the same validation contract as saved archives. Ready bytes remain immutable; runtime state and caches are separate. Hashes establish integrity, not author identity or a security sandbox.

<a id="section-26-5"></a>
## 26.5. Atomic publication

Compilation does not execute Goat initializers. The writer atomically replaces one ZIP after validation; a failed build preserves the previous destination. Native variants live inside that ZIP. Adjacent same-name DLL/SO files are neither required nor loaded. Used external dependency archives must remain accessible through module search roots.

<a id="section-26-6"></a>
## 26.6. Source-free execution

```sh
mkdir -p build/book-native/run
cp docs/book/examples/22-native-module.goat build/book-native/module.goat
./goat --compile --native required build/book-native/module.goat
cp build/book-native/module.gbin build/book-native/run/
CC=goat-no-such-compiler ./goat --run --native required build/book-native/run/module.gbin
./goat --run --native off build/book-native/run/module.gbin
```

Both executions print `7`, `6`, `4.0`. Only the ZIP is moved; the run directory needs neither the source nor a C compiler. On Windows use `.\goat.exe`, `Copy-Item` and `$env:CC`.

<a id="section-26-7"></a>
## 26.7. Failure policy

| Situation | `off` | `auto` | `required` |
|---|---|---|---|
| Corrupt metadata, bytecode, native payload or graph | Failure | Failure | Failure |
| No compatible root specialization | VM | VM | Failure before user code |
| No applicable specialization for this call | VM | VM | VM |
| Library unavailable before adapter entry | No load | Warning and VM | Failure |
| ABI violation or ordinary `RESOURCE_LIMIT` after entry | No entry | Terminal failure | Terminal failure |
| `PURE_RETRY` with a matching proof of no effects | No entry | VM retry | VM retry |

The compiler emits `PURE_RETRY` only for a proven pure body that exhausts native resources. The host also requires a matching metadata purity proof. It reuses already evaluated arguments and disables native dispatch for the retried body and its descendants. Other errors never authorize replay. `--run` never rebuilds missing artifacts.

Regression coverage includes `integration.unified_gbin`, `native.binary_program`, bytecode oracle tests, module validation, dispatch and delivery suites. See [delivery evidence](../../module-delivery-tests.md) for supported-platform verification.
