# Source graph construction (#186)

`common/module_sources.h` is the compiler-side source discovery boundary. Both
`.gmod` builds and direct `.goat` inputs use `module_sources_read`; the boolean
selects the input kind explicitly, not by suffix. The existing CLI is integrated
in #191; this API does not implement runtime `include` (#194), file scopes (#193),
build/output module-directory selection (#188/#237). Static imports consume this
graph through [module_imports_collect](module-imports-api.md) (#187).

## Ownership and ordering

A successful call returns an independently owned graph. Its `.gmod` document is
fully schema-checked and normalized before any source is opened. Defaults and
information normalization come from the common JSON reader; no local schema,
CLI override, compiler probing or dependency lookup is introduced. Unused valid
dependencies do not add files or cause archive lookup.

Each source owns its exact UTF-8 bytes, a canonical build-only absolute path,
logical name, external flag, and ordered include occurrences/target indices.
Files are numbered by iterative depth-first preorder, registering the file before
visiting its children. Main is zero. Includes inside dead branches and functions
participate; cycles stop at registered canonical paths. Discovery never executes
code. The scanner-based `parser/source_includes.h` helper shares actual Goat
literal/comment/escape tokenization with the parser and is reusable by #194.
It validates tokens, brackets and literal include operands, not the complete
surrounding language grammar, which remains a subsequent compilation phase.

Symlinks resolve before selecting the containing directory. Hardlink paths and
identical contents do not deduplicate. No state is shared between two calls.
Logical names for internal files are component-relative to the real `.gmod`
directory, or the real main directory for a direct input. External names are
basenames only and may repeat. Windows component separators become `/`; Linux
literal backslashes remain unchanged. Snapshot consumers must serialize only
logical names/external/indices, never build paths or source bytes. A ready `.gbin`
uses stored indices and never reconstructs this graph from files on disk.

## Filesystem and diagnostics

Paths are literal, without environment/tilde expansion or extension guessing.
The common filesystem API canonicalizes and reads actual opened regular objects.
Linux uses nonblocking open plus `fstat` before reading, so a FIFO without a writer
cannot hang. Windows checks the actual handle's type and directory attribute.
This rejects a special object substituted between pathname checks and opening.
It is not a multi-file transactional snapshot: callers must avoid concurrent
source-tree edits during a build.

Failures return no partial graph. Diagnostics retain a source location for an
include failure, plus the common filesystem operation, category, native code and
target path. Lexical errors refer to their own source. JSON errors precede source
I/O. Error objects require zero initialization and `clear_module_sources_error`.
Successful results require `free_module_sources`.

Default resource bounds: 65,536 files, 64 MiB per file/descriptor and 256 MiB total
source bytes. Callers may supply positive explicit bounds; these are resource
limits, not format restrictions. Filesystem size-limit failure retains its precise
operation; graph-count failure is `MODULE_SOURCES_LIMIT`. Allocator failure follows
the existing compiler fatal-allocation contract.

## Verification

`ctest --test-dir build --output-on-failure -R '^integration.module_sources$'` covers DFS diamond
and cycles, 1,800-file depth, unused neighbors/import dependencies, missing and
invalid files, literals and path separators, real `.gmod`/source symlink bases,
external sources, hardlink identity, component boundaries, direct-source mode,
strict descriptor validation before I/O, source locations and bounds. Windows
symlink cases skip only `ERROR_PRIVILEGE_NOT_HELD`; other failures remain failures.
Common filesystem unit tests cover exact/short/zero read limits and special files.
The source graph harness asserts independent snapshot ownership and leak freedom.
