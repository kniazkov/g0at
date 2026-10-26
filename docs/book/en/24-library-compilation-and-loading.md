# 24. Library Compilation and Loading

[Contents](index.md) · [Русский](../ru/24-library-compilation-and-loading.md) · [Previous chapter](23-native-abi.md) · [Next chapter](25-native-dispatch-and-vm-fallback.md)

Updated in #220 against the implemented baseline [5b98271](https://github.com/kniazkov/g0at/tree/5b98271e26cb17bdc5589aaf827316016ba2e00a). Commit-pinned links to earlier revisions below are historical implementation references; current contracts are linked explicitly.

<a id="section-24-1"></a>

## 24.1. Build, publish and execute are different stages

Goat generates C, runs an external compiler, inspects the resulting ELF/PE library, and optionally loads it for execution. Linux uses `.so`; Windows uses `.dll`. A compiler’s successful exit alone does not prove the artifact has the expected exports or requirements.

`--save-library` for a standalone source saves a companion library without running Goat code. `build app.gmod` packages bytecode and an optional native variant in one ZIP module, without loading the library or executing initializers. Running a module first validates and retains the entire dependency graph; OS loading is lazy. See [build API](../../module-build-api.md) and [loading API](../../module-loading-api.md).

<a id="section-24-2"></a>

## 24.2. Compiler discovery and target profiles

`--cc` selects one executable, not a shell command containing flags. Standalone `.goat` and `--save-library` also retain `CC`, overridden by `--cc`. Module discovery searches PATH in order: Linux `cc`, `gcc`, `clang`; Windows `gcc`, `clang`, `cc`; then supported cross-compiler candidates. It never scans the disk or mutates the inherited environment.

`--list-native-targets` lists successful compile/link probes separately from failures. Supported backend names are `linux-x86_64-gnu`, `linux-aarch64-gnu`, `windows-x86_64-mingw64`, `windows-x86_64-ucrt64`, and `windows-x86-mingw32`. A recognized name does not promise an installed toolchain. `host` describes the actual running process. `+avx2` and AArch64 `+sha2` raise requirements; `+sha2` means SHA1/SHA2, not AES/PMULL.

A probe inspects the generated binary but does not load it. `--native-probe-timeout` defaults to 10 seconds and bounds probing, not compilation of user C. Cache entries live only in the current build context. An unavailable explicit target is not silently replaced by host, and failure compiling user C after selection is not retried with another compiler.

<a id="section-24-3"></a>

## 24.3. Files, processes and publication

Processes use the portable child-process API, with explicit argument arrays and no shell interpretation. Windows process/file paths use Unicode-aware APIs. Managed working directories and relative ASCII temporary source names avoid MinGW assembler path corruption without changing the process environment.

A common cleanup process must report READY before temporary native build or extraction files are created. It owns the run’s temporary tree and tracks compiler descendants. The same service is shared by compilation and module loading; saved user artifacts are outside its tree. Its startup/READY budget is one second. A ready janitor’s later death does not change the program’s exit code, but cleanup can no longer be guaranteed.

Publication uses the common filesystem API and a same-directory atomic replacement with permission preservation. A failed module build leaves the old archive intact. This single-file guarantee must not be confused with an atomic transaction over a standalone `.gbin` plus companion library. See [filesystem](../../file-system-api.md) and [cleanup](../../cleanup-service-api.md).

<a id="section-24-4"></a>

## 24.4. Native compatibility is measured

Compiler flags preserve Goat numeric semantics (`-fno-fast-math`, `-ffp-contract=off`); target flags apply to the generated code and bundled runtime alike. Windows builds link the supported compiler runtime statically where required rather than relying on a compiler installation being present at execution time.

Static inspection derives actual CPU/OS/runtime requirements from ELF/PE and dependencies. It does not copy the build host’s glibc version into metadata. Unknown dependencies or unsupported imports fail closed. The main module hash is unchanged by native additions; each complete native library has its own SHA-256.

Delivery CI covers relocated Debug/Release artifacts and real mixed MINGW64/UCRT64 libraries. Exact control-library floors were executed on glibc 2.35/2.36 environments. Ordinary declared floors 2.14/2.17 and actual Windows 10 execution remain separate unperformed lower-bound validation, not conclusions from newer runners. See the evidence and plan in [delivery tests](../../module-delivery-tests.md).

<a id="section-24-5"></a>

## 24.5. Lazy loading and lifetime

Before user code, the entire archive graph and all native bytes are validated, even in `off` and for foreign variants. Corruption is fatal, not an `auto` fallback. In `off`, no library is selected/extracted/loaded. In `required`, the root’s own compatible variant must contain at least one specialization; dependencies do not satisfy that requirement. Individual calls without a matching specialization may still use the VM.

Only the first applicable call extracts and loads the selected module library. The loader uses retained checked bytes rather than rereading the archive. A per-library extraction/loading/ABI-binding failure is cached: `auto` warns once and uses VM for that library; `required` fails. Janitor readiness failure disables native for the whole run in `auto`, with one warning. These pre-entry failures differ from a fatal ABI violation after adapter entry.

Libraries and scopes remain alive for all program threads and descendants. Ordinary teardown unloads safely; whole-process `exit` does not unload underneath active native threads. Loading is not a sandbox: OS initializers may execute before post-load checks. `goat-inspect` offers an isolated probe, not security isolation for malicious code.

<a id="section-24-6"></a>

## 24.6. Standalone commands and module commands

From the repository root, with a suitable compiler:

```sh
mkdir -p build/book-native
cp docs/book/examples/22-native-module.goat build/book-native/module.goat
./goat --save-library build/book-native/module.goat
./goat --native required --print-native build/book-native/module.goat
./goat --list-native-targets
```

The first Goat command saves `module.so` on Linux or `module.dll` on Windows; the second prepares its own temporary library, not that saved companion. PowerShell equivalents use `New-Item -ItemType Directory -Force`, `Copy-Item`, and `.\goat.exe`.

For a named module use `goat build app.gmod --native required`, then run the resulting `modules/<name>/<version>.gbin` without a compiler. Chapter 30 supplies complete `.gmod` examples. Build, loader, toolchain, cleanup and delivery suites in CI check the distinct stages; a `ready` report is preparation evidence, whereas native counters establish actual adapter execution.
