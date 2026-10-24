# Ordered module directory search (#188, #237)

`vm/module_search.h` supplies the shared lookup primitive for build and run.
It is an internal, nonexecuting API, not yet CLI integration (#191/#218), graph
traversal (#189), or native library loading. Callers create one search object
for the root program and reuse it for every dependency; a library's directory
must never be appended to that object's paths.

## Fixed roots and directory order

`prepare_module_search(root, module_dir, environment, ...)` resolves the existing
root `.gmod`, `.goat`, or `.gbin` through symlinks before deriving its parent.
The ordered set is:

1. `modules` beside that real root file;
2. the explicitly supplied module directory, if any;
3. the supplied `GOAT_MODULE_PATH` snapshot, split on `:` on POSIX or `;` on Windows.

The environment argument is a value, not an environment variable name; NULL
means absent. Empty list elements are ignored. Relative explicit/environment
paths are anchored to the initial working directory, not the root file's parent.
Paths are literal: no shell, variable or tilde expansion. Existing aliases are
canonicalized and deduplicated, preserving first occurrence. Unavailable directories retain their absolute spelling and defer inspection
errors until lookup actually reaches them. Directory
contents need not exist at preparation time. A later cwd change or retargeting of
the root symlink does not alter the fixed root or directory set. UTF-8 filesystem
operations use the shared platform adapter, including Windows wide APIs.

## Exact first-candidate lookup

`find_module` checks only `<directory>/<name>/<version>.gbin`, in order. Portable
name/version grammar is validated before filesystem access. No directory scanning,
version selection, recursive search or preference for a later native variant is
performed. Definitively missing candidates continue; inspection/read errors stop.
The first existing candidate is read once as a checked regular-file snapshot and
passes the entire common module validator, including integrity/native metadata.
Corruption, wrong metadata identity or an optional expected-mainHash mismatch is
terminal: a valid later copy must not conceal the problem. Candidate symlinks are
followed by the checked reader; broken symlinks and nonregular files are errors.

Success owns an independent `validated_module_t`, valid after freeing the search
object or changing/removing its archive. Caller limits/report callbacks are passed
to the common validator; reading is bounded by `max_archive` and the archive's
32-bit representation. A read-limit failure retains the filesystem cause.
No dependency graph is traversed and no code or native library is executed.

Initialize `module_search_error_t` with zero and release it with
`clear_module_search_error`; each operation clears previous contents. Errors retain
the exact ordered list of candidates actually inspected, the underlying filesystem
failure or validator result, and a distinct status for absence, identity and hash
mismatch. Preparation failure and lookup failure clear the output pointer. NULL
error pointers are supported. Release search objects with `free_module_search`
and successful snapshots with `free_validated_module`. Allocation follows the
existing fatal-OOM policy; recoverable loader OOM remains #236. Dependency-chain enrichment remains tracked in #190.

`scripts/check_module_search.py build/module_search_testing` exercises directory
precedence, aliases, platform separators, real-root `.gmod`/`.goat`/`.gbin` behavior,
fixed cwd/symlink roots, exact/nonrecursive lookup, first-candidate failure and
snapshot ownership. Linux and MinGW CI run the same fixture suite.

## Startup paths and strict failures (#237, decisions 49–55)

At startup, call `prepare_module_search_from_environment(root, module_dir, ...)`
to capture `GOAT_MODULE_PATH` through the UTF-8 platform adapter. It accepts both
an unset variable and an empty value; empty components never imply cwd. Use an
explicit `.` to include cwd. The explicit-snapshot API remains available for
embedding/testing. Both APIs fix relative environment entries and `--module-dir`
against the cwd at preparation, not against the main file's parent. Later cwd
or environment mutations cannot redirect the retained search set. Capture must
happen once at program startup; CLI/graph wiring remains the subsequent tasks.

Only reliably absent paths permit trying the next directory. Access-denied
files/directories, a file used as a directory, and dangling intermediate links
stop lookup. Unused directories do not need to exist or be accessible. Existing
candidate read failures are terminal even when the OS reports disappearance
between inspection and read. A valid copy later in the list never masks failure.

`format_module_search_error(error, name, version, consumer)` returns an owned
English diagnostic (release with `FREE`). It distinguishes missing dependencies,
filesystem failure, invalid/corrupt modules, identity mismatch and mainHash
mismatch. The message includes the requested identity, supplied consumer, only
actually checked paths in order, and the stopping path for candidate failures.
Filesystem diagnostics include a readable category, operation, exact path and
native error code; validation diagnostics retain their category/failure count.
Detailed validator callbacks remain available through `find_module` options.
No graph-derived consumer information is invented by this primitive.

The shared fixture suite covers every rule 49–55 on Linux and Windows, including
real denied-read and denied-directory access (chmod/unprivileged POSIX process
or scoped Windows ACL restored in `finally`). CI requires permission checks to
be enforced; unsupported local filesystems report that limitation explicitly.
