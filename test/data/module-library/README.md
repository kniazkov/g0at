# Real mapped module-library migration (#368)

`test/src/functional/module_library_scenarios.c` replaces the library half of
`scripts/check_module_library.py` at baseline `64ef1dc4`. The companion native-build
runner is documented in `../module-build`. No compiled library or descriptor
expectation is frozen: every selection builds a real required-native Goat module,
extracts its actual archive metadata/library through the independent ZIP helper,
and exercises the production mapped-range loader in a separate component process.

`main.goat` is the exact original UTF-8 source, including no trailing newline.
The `.gmod` has the same fields/values as Python's descriptor. For manufactured
libraries, an independent canonical JSON serializer produces the same sorted,
compact UTF-8 descriptor bytes as original `json.dumps(sort_keys=True,
separators=(',', ':'), ensure_ascii=False)`. The envelope retains `GOATND01`,
uint64 little-endian byte length and the trailing NUL. Generated C declarations,
compiler flags, platform export decorations and symbol lists match the original.
All memory permissions, symbol ownership and constructor side effects are real.

## Stable case map

All IDs below have prefix **`integration.module_library.`**. All use the combined
`module-build-library-migration` label. There are **26 IDs** on every platform;
15 ELF-only selections explicitly skip on Windows, leaving its original 11 checks.

| Suffix | Original expected result / property | Platforms |
|---|---|---|
| `original` | Load actual producer library and resolve every declared adapter | All |
| `valid` | Real fabricated const descriptor and function exports accepted | All |
| `missing-descriptor` | No descriptor export rejected | All |
| `writable-descriptor` | Writable descriptor memory rejected | All |
| `bad-magic` | `INVALID!` envelope rejected | All |
| `overflow` | UINT64_MAX encoded size rejected | All |
| `outside-image` | Claimed 1-GiB descriptor beyond mapped image rejected | All |
| `unterminated` | Replaced trailing NUL rejected | All |
| `different-name` | Every `mapped` byte sequence changed to `otherx`, rejected | All |
| `missing-last-symbol` | Missing declared adapter rejected | All |
| `data-symbol` | Data object exported as adapter rejected | All |
| `data-in-text` | Data object in executable `.text` remains rejected | Linux ELF |
| `valid-sysv`, `valid-gnu`, `valid-both` | Valid real library for each ELF hash-table style | Linux ELF |
| `data-in-text-sysv`, `data-in-text-gnu`, `data-in-text-both` | Hash-style-specific data-in-executable-memory rejection | Linux ELF |
| `object-alias-sysv`, `object-alias-gnu`, `object-alias-both` | Proven actual function alias with only its type changed to STT_OBJECT rejected | Linux ELF64 LE |
| `function-alias-sysv`, `function-alias-gnu`, `function-alias-both` | Same proven function alias left STT_FUNC accepted | Linux ELF64 LE |
| `unreadable-descriptor` | Aligned descriptor plus 8192 bytes, actual constructor mprotect(PROT_NONE), rejected | Linux ELF |
| `foreign-owner` | Dependency exports descriptor, caller exports adapters; lookup success does not establish ownership | Linux ELF |

Linux x86-64/AArch64 and glibc/musl retain the exact original ELF branches. In
particular, musl does not require `dladdr1`: the three real linker hash-table
styles still test the portable loader implementation. Windows MinGW
32/64/UCRT retain the non-ELF cases and native DLL export syntax. The Windows
skip reason explicitly identifies unavailable ELF/mprotect fixture semantics;
compilation, process cleanup or loader failures never become skips.

### Bounded ELF alias proof

The fixture reader requires ELF64 little-endian, bounds the section table and
all referenced dynamic-symbol/string tables, requires 24-byte symbol entries,
and strictly decodes symbol names as UTF-8. Both the requested alias and
`actual_function` must be unique, defined STT_FUNC symbols with the same nonzero
address and section identity. Object cases change only the alias's `st_info`
low nibble. Prefix/suffix comparisons prove all other image bytes unchanged;
the actual function's complete 24-byte entry is separately preserved. Both
object and function cases reread the physical file and compare every byte.
This is adversarial adapter metadata, not a replacement for real compiled-native
delivery evidence.

## Release dependency

The original built Release component failed the first genuine-library check with
`Unexpected loader result: Invalid module-v1 loader arguments`: `assert` had
removed metadata parsing. The component also kept adapter and mapped-range checks
inside assertions. `module_library_testing.c` now undefines `NDEBUG` before its
first headers, keeping setup and checks active against unchanged Release
production libraries. No production loader/format behavior is modified.

## Reproduction and resources

```sh
cmake --build build --target module_library_scenarios --parallel 4
ctest --test-dir build -R '^integration.module_library\.' --output-on-failure --parallel 4
build/module_library_scenarios integration.module_library.object-alias-gnu \
  build/module_import_execution_testing build/module_library_testing \
  /usr/bin/cc build test/data --keep-work
```

Substitute `--list` for the ID to enumerate all 26 selections. The compiler is
one executable path, as in the original script, not a shell command. Each ID has
an independent workspace under build, read-only source fixture, 300-second shared
deadline and 330-second outer CTest allowance. The largest path preserves the
original 120-second native build + two 60-second compiles + 30-second loader
budget. Shared process helpers retain complete output and clean owned children;
failures and `--keep-work` preserve generated C, descriptor, source and actual
libraries for inspection. No runner writes into test/data.

## Local evidence

- C library selections: **26/26 Debug, Release and UBSan**.
- Unchanged original Python: **26 loader checks**, Debug and Release after the
  test-only assertion fix. Pre-fix Release failure was recorded first.
- All 26 C selections in both configurations were compared with retained original
  artifacts: exact Goat source, complete `module.json` bytes and every generated
  adversarial C source agree. The original Python `check()` subprocess contract
  was replayed on each actual C-produced library and metadata pair.
- Five semantic negative controls in each configuration reject intentionally
  inverted real-loader expectations, a writable descriptor in the nominal valid
  case, repaired magic in the expected-invalid case, and a supposed function
  alias implemented at a different address. The first four report an actual
  unexpected loader decision; the last reaches the bounded ELF identity check.

The parent migration runs the combined suite, fresh target-only Release build,
style and cross-platform CI. Those exact-head CI links belong in the PR. The
retired Python runner exports no helpers consumed by other scripts.
