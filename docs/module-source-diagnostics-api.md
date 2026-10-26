# Compiled source diagnostics (#234)

The existing v1 bytecode location runs preserve one-based Unicode-scalar line and
column positions and function IDs. Function records now retain the diagnostic
name of a direct declaration initializer; aliases do not rename code. Anonymous
functions use the existing `0xffffffff` name sentinel. Empty function bodies keep
their definition coordinates. Function IDs remain source-order identities assigned
before optimization; no new JSON fields or binary sections are introduced.

`format_exception_trace(process, trace)` resolves the existing immutable origin
frames against the process-owned, checked module/file/bytecode mapping. It returns
an owned string (`FREE_STRING`) and never reads source files or reopens archives.
The process must still own the referenced module mappings. Missing mappings are
reported as unknown numeric locations rather than guessed names. Trace identity
itself remains retained independently of the process lifetime.

Example:

```text
  at geometry@1.2.0: src/vector.goat:24:9, length
  at geometry@1.2.0: math.goat (external file #2):8:5, <anonymous>
```

Names are escaped without truncating embedded controls. External markers and
indices disambiguate equal basenames; module name/version disambiguates imports.
The runtime uses exact module identity, not the printed name or file number, to
resolve a frame. `<top-level>` and `<anonymous>` are explicit fallbacks. `0:0`
retains the v1 meaning of compiler-generated code; no nearest source line is
invented. Source text and build-machine absolute paths are not serialized.

Terminal native backend failures preserve a callee-origin frame before returning
to the caller, using the same trace and mapping. `[native entry]` explicitly marks
the callee body-entry location: the current native ABI returns a status, not an
exact failing expression location. This does not convert a backend failure into
a catchable Goat exception, change pure resource-limit retry policy, or execute
a failed body again. Normal VM exceptions retain their exact throwing instruction;
failed initialization retries keep the original trace, not a later caller's site.

The source/compiled-module component runner and the module CLI (#191) print this
shared diagnostic. See [CLI commands and checks](module-cli-api.md).

Verification: `check_source_include.py` covers producer names, nested/anonymous
identities, empty locations, builder growth and relocation. The new
`check_module_source_diagnostics.py` executes compiled archives with external
includes, duplicate basenames, imported modules, Unicode and escaped labels,
saved failures, GC/repeated initialization, and deleted archives/source trees.
Existing native-call tests verify native callee/caller origin and ownership.
Both Linux architectures/compilers and Windows CI run the integration checks.
