# 26. Separate Compilation

[Contents](index.md) · [Русский](../ru/26-separate-compilation.md) · [Previous chapter](25-native-dispatch-and-vm-fallback.md) · [Next chapter](27-observability.md)

Revision 7. Implementation described: [commit 64c80b9](https://github.com/kniazkov/g0at/tree/64c80b96ce695db13867266653f3fc6c0416dc26).

<a id="section-26-1"></a>

## 26.1. Prepare now, run later

Separate compilation here means saving a prepared program and running it later with a separate command. It is not a mechanism for importing source modules or independently linking parts of a Goat program.

`--compile` creates a `.gbin` containing bytecode and, after successful native preparation, a companion `.so` or `.dll`. It does not execute the program body: output and input wait until `--run`. Later execution needs the Goat interpreter, but no longer needs the original `.goat` file or a C compiler.

The saved artifact is a VM program with an optional native addition. A missing library can therefore be handled under `auto`, while `off` executes saved bytecode without loading it.

<a id="section-26-2"></a>

## 26.2. Three file regions

The format is implemented in [binary.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/vm/binary.c). An outer header occupies 96 bytes. It is followed by an inner bytecode image and a native-binding table. Total size is capped at 256 MiB.

| Offset | Size | Outer-header field |
|---:|---:|---|
| 0 | 8 | Signature `GOATBIN3` |
| 8 | 8 | Format version, currently 3 |
| 16 | 8 | Platform tag |
| 24 | 8 | Bytecode-image size |
| 32 | 8 | Binding count |
| 40 | 8 | Reserved, 0 |
| 48 | 8 | Reserved, must be 0 |
| 56 | 8 | Checksum of the complete `.gbin` |
| 64 | 32 | Library SHA-256; zero bytes without bindings |

Numeric outer-header fields use little-endian encoding (least significant byte first). Checksum calculation treats the field at offset 56 as zero-filled. The algorithm is 64-bit FNV-1a from [binary_file.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/lib/binary_file.c).

The inner image is chapter 8's buffer: its own header, instructions, data descriptors, and data. It is not another serialization of the AST. Analysis proofs, VM-object addresses, and native-code pointers are not persisted.

Each binding occupies 16 bytes: two little-endian `uint64` values, the `FUNC` instruction index and the library's `function_id`. Entries are ordered by increasing instruction index. One binding represents a function that may have several specializations.

<a id="section-26-3"></a>

## 26.3. What compatibility means

The platform tag combines `wchar_t` size and a byte-order indicator. Inner instructions and data retain the current implementation's representation, including wide strings; outer little-endian encoding does not make the whole image portable.

The loader additionally requires a suitable `double` representation and validates the inner-image signature and structure. A native library has the OS, architecture, and ABI requirements described in earlier chapters.

> [!CAUTION]
> `.gbin` is not a universal cross-platform format. The platform tag does not contain a complete CPU, OS, or build identifier. Matching tags do not promise compatibility between arbitrary implementations, and version 3 is not a commitment that every future Goat version will read the file.

<a id="section-26-4"></a>

## 26.4. Checks before execution

First come file size, outer signature, version, platform tag, reserved field, checksum, and exact agreement of region sizes. Counts are checked before multiplication and pointer construction so that a corrupt length cannot become an address outside the buffer.

Next come inner offsets and record sizes, data ranges, valid instruction codes, and zero flags. Jumps must target the instruction array. `ILOAD64`, `RLOAD`, and `FUNC` require a preceding `ARG`; `FUNC` also requires a valid body address and parameter names. String references must designate correctly aligned, null-terminated strings without embedded nulls.

Bindings must reference `FUNC`, have a positive `function_id`, and have strictly increasing indices. This ties the saved table to particular bytecode, not just to a source function count.

> [!CAUTION]
> These checks are not a complete bytecode verifier: they do not prove correct stack depth and every control transfer in an arbitrarily constructed program. FNV-1a is not a cryptographic signature. Someone modifying a file can recompute its checksum; a successfully checked untrusted file does not become safe to execute.

<a id="section-26-5"></a>

## 26.5. The library must belong to this program

Native compilation stores the SHA-256 of the completed library's exact bytes in `.gbin`. `--run` locates a companion with the same base name and the platform's extension. Its bytes are first read under a size limit and SHA-256 is compared before loading the library or executing its initializers. The checked bytes are then written to a private temporary directory, and that copy is loaded.

This snapshot removes the gap between validating one file and loading different contents through the same path. After loading, ABI, function entries, and parameter agreement with `FUNC` instructions are checked again. All restored entries must be pure and have no named binding. Failure to install any binding clears those already installed.

This checks integrity and pair consistency, not file provenance. If both files are changed and checksums recomputed, the author's authenticity cannot be established. Releasing the library removes its temporary snapshot.

Publishing an individual file in [binary_file.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/lib/binary_file.c) uses a temporary file beside the destination followed by replacement. The two output files nevertheless remain two operations.

> [!CAUTION]
> Publishing `.gbin` and its native library is not one atomic transaction. Failure between replacements can leave a mismatched pair. The loader detects the mismatch, but there is no automatic restoration of the previous pair.

<a id="section-26-6"></a>

## 26.6. Testing execution without source

Use the working copy `module.goat` from chapter 22. On Linux:

```sh
./goat --compile --native required build/book-native/module.goat
mkdir -p build/book-native/run
cp build/book-native/module.gbin build/book-native/module.so build/book-native/run/
CC=goat-no-such-compiler ./goat --run --native required build/book-native/run/module.gbin
./goat --run --native off build/book-native/run/module.gbin
```

The `run` directory has no source. The first run command also specifies a nonexistent compiler: it is never invoked. Both execution commands print:

```text
7
6
4.0
```

The equivalent PowerShell sequence, after preparing the working copy as in chapter 24:

```powershell
.\goat.exe --compile --native required .\build\book-native\module.goat
New-Item -ItemType Directory -Force .\build\book-native\run | Out-Null
Copy-Item .\build\book-native\module.gbin, .\build\book-native\module.dll .\build\book-native\run\
$previousCC = $env:CC
try {
    $env:CC = "goat-no-such-compiler"
    .\goat.exe --run --native required .\build\book-native\run\module.gbin
} finally {
    $env:CC = $previousCC
}
.\goat.exe --run --native off .\build\book-native\run\module.gbin
```

To save bytecode alone, use `--compile` without enabling native mode. An old library beside it does not add bindings to the new `.gbin`: that decision is recorded inside the program.

<a id="section-26-7"></a>

## 26.7. Failures and boundaries of separate execution

| Situation | `off` | `auto` | `required` |
|---|---|---|---|
| Corrupt or incompatible `.gbin` | Failure | Failure | Failure |
| Valid bytecode, no native bindings | VM | VM | Failure before execution |
| Bindings exist, library is missing or mismatched | VM without loading | Diagnostic and VM | Failure before execution |
| Valid pair | VM | Native dispatch | Native dispatch |

`--run` neither reconstructs the source nor attempts to rebuild a missing library. Even with a valid pair, individual calls may remain in the VM or fall back after a resource limit, as described in chapter 25.

The CLI sequence is in [binary_program.c](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/cli/binary_program.c); the saved-program interface is in [binary.h](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/src/vm/binary.h). [check_binary_program.sh](https://github.com/kniazkov/g0at/blob/64c80b96ce695db13867266653f3fc6c0416dc26/scripts/check_binary_program.sh) checks moving pairs to another directory, execution without source or compiler, missing and mismatched libraries, damaged files, and failure modes. This completes the path from a proven specialization to a reusable executable artifact. The next part addresses observing, testing, and measuring this implementation.

Format version 3 is incompatible with version 1: old `.gbin` files must be recompiled. SHA-256 binds the pair when the bytecode is trusted; it is not a digital signature and does not prevent replacement of both files and the hash. The container checksum remains FNV-1a for corruption detection.
