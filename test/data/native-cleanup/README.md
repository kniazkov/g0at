# Native cleanup migration (#381)

## Independent literal fixtures

Seven byte-exact inputs are frozen from `scripts/check_native_cleanup.py` at
`b953d9f797e1aa7cd660370a06fbd36d649c3535`. Source AST literal expressions,
the original explicit-exit suffixes, and the original
`json.dumps(dict(formatVersion=1, name='cleanup-module', version='1', main='main.goat'))`
expression supply these files. No production serializer, compiler output or hash
implementation generates the oracle. `provenance.json` records byte counts and
SHA-256 hashes. Source text intentionally has no final newline; `module.gmod`
retains Python's key order and spaces. Scoped `-text` protects all runtime inputs.

| Fixture | Original use |
| --- | --- |
| `program.goat` | synchronized READY/input/9; normal code and normal completion |
| `empty.goat` | SIDE must not execute when required native inventory is empty |
| `code-exit.goat` | program plus literal `exit(23);` |
| `module-normal.goat` | module main: 9, READY/input, 10 |
| `module-exit.goat` | module main plus exit23 inside try/finally; UNREACHABLE forbidden |
| `module.gmod` | cleanup-module version1 descriptor, main.goat |
| `export.goat` | identical original saved-library and separated compile inputs |

Runtime copies fixtures into fresh scenario-owned work and never edits the corpus.
Every module case has an independent producer build before its consumer; splitting
requires eight producers instead of the original two shared producers, but retains
all producer checks and all eight consumer combinations.

## Complete case map and acceptance

`mapping.json` lists all 22 stable `integration.native_cleanup.*` IDs and their
independent expectations: one empty-inventory case; four pre-READY cleaner fault
combinations; four cleaner-death native mode/exit combinations; two normal/exit
completion cases; eight descriptor/archive, auto/required, normal/exit module
combinations; two exported artifact cases; and one two-owner interruption case.
These split the original eight aggregate `ok(...)` groups without dropping loops.

No scheduler delay proves readiness: owner stdout lines, marker content, pinned
process state and actual filesystem inventory establish barriers. READY user
output alone is insufficient to establish native execution: death cases require
native artifacts; module consumers additionally require an alive owner/cleaner
and nonempty regular loaded-library inventory before stdin release.

The owner uses independent raw OS pipes/launch/reap, not the production supervisor;
otherwise supervisor descendant cleanup could mask a failing cleanup service.
PID markers must be pinned at the live barrier before signals or death waits.
Owner failure cleanup must not turn missing observations into passing results.

## Observation details

The original `ProcessWatch` uses Linux pidfd readiness or Windows handle waits:
observed process exit (including a zombie) is distinct from raw PID existence.
It never reopens a bare PID to signal it. Default process waits are 10 seconds;
module completion and final owner cleanup allow 40 seconds. Owner lines/markers
and ordinary owner completion allow 30 seconds; empty-inventory stdout EOF allows
2 seconds and post-module EOF allows 5 seconds. Forced owner reap allows 10 seconds.

The interruption case compares `compiler.output.parent.parent.parent` to the owner
root by filesystem identity, not lexical path normalization; this preserves Windows
drive case/extended-prefix aliases while enforcing exact three-level containment.
Both trees are pinned before owner A dies. A's compiler/worker/cleaner must exit
before its work directory disappears, and B's three processes must remain alive
until B itself is terminated. The unpublished destination must never appear.

Case-specific stderr policies are preserved: every normal end rejects any ASCII
case-insensitive `leak`; fault cases require exactly one cleanup warning but allow
other diagnostics; successful native/library/module cases require empty stderr.
The empty-inventory rejection additionally requires nonempty stderr and immediate
stdout EOF, proving SIDE did not execute. No unrestricted retries or sleeps replace
those checks. Local inability to observe real cleanup must be reported, not skipped.

## Local validation and independent controls

The seven fixture hashes and byte counts match `provenance.json`. Independent
fake-owner controls passed 48/48 across Debug and Release (24 per configuration):

- Empty inventory: valid rejection accepted; wrong status, SIDE output, cleaner
  startup marker, leftover workspace and mixed-case leak diagnostic rejected.
- Cleaner pre-READY fault: valid auto/required behavior accepted; missing/duplicate
  cleanup warning, compiler query and leftover workspace rejected. Raw non-UTF-8
  stderr after the required warning and otherwise unconstrained required-mode
  stdout are deliberately accepted, matching the original byte-stream policies.
- Saved/compiled outputs: nonempty library and bytecode accepted; missing or empty
  artifacts, lingering work and unexpected stderr rejected. Unspecified successful
  stdout remains accepted. These controls check the oracle without using the
  production implementation to generate expected outcomes.

The unchanged Python Debug run passed its first seven aggregate groups but failed
in interrupted-build at original line 244: the compiler workspace still existed
following owner/compiler/worker/cleaner exit. This local executor's inherited
cleanup limitation is not hidden or claimed green. Full C/original remote parity
is required before deleting Python; the runner's real-platform results are reported
with the PR evidence rather than inferred from the fake-owner controls.

Independent module IDs snapshot the produced archive before consumption and require
byte-for-byte identity afterwards, not only continued existence. This preserves
the original shared archive's reuse invariant when each consumer now has its own
producer setup; no production hash function supplies this check.

An adversarial EOF control exits with the correct empty-inventory rejection but
leaves stdout open in a child for four seconds (stderr already closed). The original
Python rejects it at its independent two-second stdout EOF deadline. The initial
C transport incorrectly accepted it by draining both streams under the longer
owner wait; the corrected runner waits separately for owner exit/stderr and retains
the original stdout-specific deadline. This control is included in both builds.

### Final local evidence before publication

- C runner: **21/22** in each of Debug, Release and UBSan. Only
  `interrupted-build` fails because the compiler workspace remains after the
  observed processes exit; this is not reported as a green suite.
- Unchanged Python: **7/8 aggregate groups** in all three configurations, failing
  at the same remaining-workspace assertion. No migration check was relaxed to
  hide that shared local limitation; full remote parity remains the gate.
- Independent controls: **111 passed** in total: 48 owner/protocol/artifact
  controls across Debug and Release; 45 deep filesystem identity, suffix and
  report controls across Debug, Release and UBSan; 12 module descriptor/archive
  corruption and hidden-library controls across Debug and Release; and six
  Windows drive, extended-prefix and UNC lexical-normalization controls.
- Those six lexical controls compile the actual helper fragment with its Windows
  branch selected **on Linux**. They verify the path transformation logic, not
  Windows OS APIs or execution; native Windows CI is still required.

Final review accepted the explicit Windows path conversion and lexical-parent
handling while retaining filesystem-identity comparison and exact three-level
containment. Fixture byte hashes remain unchanged.
