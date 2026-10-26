# Module formats v1

[Русский](README.ru.md) · [language contract](../book/en/modules.md)

This is the wire-format specification fixed by #244. #250 implements Unicode
strings and #251 implements the shared bytecode memory-image codec. Module ZIP
packaging, JSON/table cross-validation and module execution remain separate tasks.
Earlier illustrative fragments in the language contract are not complete files.
The closed schemas here fix field spellings; the semantic constraints below and
the language contract are mandatory in addition to JSON Schema shape checks.

## Deliverables and versions

`gmod.schema.json`, `module.schema.json`, `native.schema.json`, and
`target.schema.json` are generated from the reviewed `schema.py`. Every property
appears there with its type, requiredness, range and closed nested object shape.
Schema omission does not authorize new fields. `reference.py` fixes canonical
bytes and generates the text/hex vectors under `vectors/`; `test_reference.py`
checks independent expectations and malformed inputs. Hex files encode actual
binary files, not additional container entries: decode hex to obtain `.gbin` or
`bytecode.bin`. All text files in this directory use UTF-8/LF.

Description, module container, bytecode, analysis, embedded native descriptor and
native call ABI each start at version 1 and evolve independently. Module version
strings remain opaque identifiers. No reader for the previous `GOATBI11` /
`goat v.1` images is required. New bytecode magic is `GOATBC01`; this avoids
confusion with old images even when their numeric version was 1.

## JSON fields and semantic constraints

All JSON is strict UTF-8 without BOM, duplicate decoded keys or unpaired
surrogates. Integer fields use integer tokens, not fractional/exponent notation,
strings or an intermediate double. JSON Schema alone cannot enforce token
spelling or duplicate keys: the parser must do so. `-0` is accepted and
canonicalizes to `0`. No JSON floating-point numbers occur in v1.

| Object | Required | Optional / absence |
| --- | --- | --- |
| `.gmod` | formatVersion=1, name, version, main:string | dependencies={} defaults; native={mode:auto,target:host}; description, license, homepage, authors absent |
| `module.json` | formatVersion=1, bytecodeVersion=1, name, version, mainHash, main:uint32, files | dependencies=[], functions=[], externals={}, native=[], description, license, homepage, authors=[], producers=[] |
| file | name, entry:uint32, external:boolean | none; identifier is array index |
| function | functionId:uint32 nonzero, file:uint32, instruction:uint32, parameters | analysis absent |
| parameter | name | none; position is significant |
| external | mutable:boolean | type, value, functionId OR functionRef absent = unknown |
| dependency | name, version, hash | none; hash pins the dependency's mainHash |
| functionRef | module, version, functionId | no hash; resolve in the reachable pinned graph |
| analysis | formatVersion=1, summaries | none |
| summary | when | result, effects, normalReturn:false absent = no corresponding claim |
| when | none | arguments absent = unconstrained; if present, exactly formal arity |
| ValueFacts | none | type, nonNull:true, value, range, elements, empty:true |
| effects | may, unknown:boolean | none |
| native variant | library, hash, goatAbiVersion, target, specializations | cCompiler absent |
| specialization | specializationId, functionId, symbol, signature, when | none |
| embedded native JSON | formatVersion=1, module, version, mainHash, goatAbiVersion, target, specializations | none; no library hash |

Names/versions use the agreed ASCII grammar and 255/250-byte limits. Reject
reserved DOS stems con/prn/aux/nul/com0..com9/lpt0..lpt9 even with extensions.
All records and keys are validated before CLI overrides or normalization.
`.gmod.main` is a nonempty literal OS path without U+0000, relative to the real
descriptor directory unless absolute; no environment or tilde expansion.

File names use the existing internal/external naming rules. Internal names are
unique; external basenames may repeat. `main` and `file` must exist. Entries and
FUNC addresses must agree with the binary tables. Compiler numbering is the
agreed include-depth-first file numbering and source-order function numbering.
Removed functions leave ID gaps. Every retained FUNC has exactly one function
record and vice versa. Parameter names/order agree with its bytecode vector.
Dependencies list exactly syntactic imports, including unreachable branches.
Reject duplicate name/version pairs even with equal hashes. References do not
change scope, initialization, closure identity or access rights.

Mutable exports cannot promise a type, scalar value or fixed function binding:
external writes may replace the value. Fixed function bindings require type
`function` if type is present; own IDs must exist. A functionRef names a reachable
dependency and existing function; this check belongs to graph validation, not
the autonomous archive validator used by merge. Own declarations and the entry
file's external interface must agree with externals. Unknown facts retain the
export record and its mutable flag.

ValueFacts types are null, boolean, integer, real, numeric, character, string,
function, array, object. `object` means a user-defined object. `value` requires
an explicit scalar type. Integers are int64; reals are exactly 16 lowercase hex
digits containing binary64 bits (including signed zero and NaN payloads);
characters are one Unicode scalar in a JSON string; strings preserve all
characters including U+0000; boolean values are JSON booleans. `type:null` may
have only JSON null as value; canonical output removes that redundant value.
Ranges require integer type, ordered inclusive int64 bounds and consistency
with value. `elements` and `empty:true` require array type; nonNull contradicts
null. Validate nested facts even when normalization will remove them.

Summaries have the agreed universal proof semantics, including captures and
extra/missing argument behavior. normalReturn:false prohibits even an empty
result field. Effects are unique members of input/output/externalRead/externalWrite;
unknown:false means a complete bound in that effect system. No claims of
termination, allocation freedom or detailed capture identity are implied.
The initial producer must not invent facts absent from its existing analysis.
Analysis-overlap is permitted when facts agree; detectable contradictions are
errors, and the producer remains responsible for truth. The loader does not
repeat static analysis to re-prove summaries.

## Canonicalization and identity

Validate the complete input before discarding any field. Perform recursive fact
normalization first: exact null loses value; redundant nonNull is removed with
an explicit non-null type; integer singleton ranges become value, full int64
ranges disappear, a consistent range alongside value disappears. Normalize
elements recursively, then remove empty elements or all elements when empty:true.
No other reductions are implicit in v1.

Normalize summaries: preserve positional arguments; remove the entire arguments
array only if all normalized elements are empty and its original arity was
correct. Remove empty result. Order effects.may as externalRead, externalWrite, input,
output; remove effects only when may=[] and unknown=true. Compare whole
normalized summaries as canonical bytes and reject duplicates **before**
removing when-only summaries. Sort retained summaries by canonical unsigned UTF-8
bytes. Remove an analysis container whose validated summaries become empty.

Sort functions numerically by functionId and dependencies by unsigned ASCII
(name,version). Sort producers by unsigned UTF-8 (name,version,buildId), reject
duplicates. Authors preserve order, reject duplicate (name,email) after empty
email removal. Empty description/license/homepage and empty authors/producers,
dependencies/functions/externals/native are omitted. Empty program constants are
never omitted. Other required arrays, including parameters and files, remain.
Normalize targets by removing trailing zero version components (retain at least
one), sorting unique CPU features; normalize native guard conditions by canonical
bytes, rejecting duplicates and omitting an empty conditions array. Sort
specializations by numeric specializationId; variants by canonical target bytes,
ABI version and library hash, then unsigned UTF-8 library name to break equal
keys deterministically. This final tie-break only orders serialization; filenames
do not select a platform.

Canonical JSON sorts object keys by decoded unsigned UTF-8 bytes; arrays preserve
their already normalized order. No whitespace or BOM. Quote/backslash use their
two-byte escapes; U+0000..001F use lowercase `\u00xx`; all other scalars are direct
UTF-8, including `/`. Decimal integers have no leading zeros or plus sign.
Normalization is idempotent. No Unicode normalization, locale comparison or
floating-point round trip is allowed.

The main metadata projection is **exactly**:
formatVersion, bytecodeVersion, name, version, main, files, dependencies,
functions, externals (omit absent optional fields). No other field participates.
Thus mainHash, native and all information fields are excluded. Do not omit
analysis, mutable flags, function references or dependency hashes.

The hash domain is 18 bytes: the 16 ASCII bytes `GOAT-MODULE-MAIN`, then `00 01`.
SHA-256 input is this marker, uint64-LE canonical-metadata byte length,
canonical metadata, uint64-LE bytecode byte length, exact bytecode. The result is
64 lowercase hex digits. Library hashes cover every original DLL/SO byte.
`vectors/hashes.json` contains the entire minimal hash input in hex, independent
component hashes and the resulting archive hash. No self-reference is present.

## Direct-use bytecode image

Execution supports little-endian, 8-bit bytes, 32-bit Unicode scalars, int64 and
IEEE binary64. The loader allocates an at-least-8-aligned buffer, validates it
and creates external const views. Finished bytes never change. No pointers,
native size_t, unspecified padding or wchar_t are serialized. Build-time
sizeof/offsetof/alignment assertions verify any C structs used to access it.
The schema limits do not guarantee that a 32-bit process has enough memory.

All numeric fields below are unsigned LE unless stated otherwise. Byte offsets
are from image start except data-descriptor offsets, which are from payload.
Header size is 64 bytes; each named field has one fixed position:

| Offset | Width | Field |
| --- | --- | --- |
| 0 | 8 | ASCII GOATBC01 |
| 8 | 4 | version=1 |
| 12 | 4 | headerSize=64 |
| 16 | 4 | totalSize, exact file length |
| 20,24,28,32,36 | 4 each | instructionCount, dataCount, fileCount, functionCount, locationCount |
| 40,44,48,52,56,60 | 4 each | instructionsOffset, dataDescriptorsOffset, filesOffset, functionsOffset, locationsOffset, payloadOffset |

Sections are contiguous in the listed order: 8-byte instructions, 16-byte data
descriptors, 16-byte files, 32-byte functions, 24-byte location runs, then payload.
Offsets must equal these calculated boundaries; there is no inter-section
padding. Counts include every record; instructions/files are nonempty. Arithmetic
is checked before addressing memory. Every section boundary is a multiple of 8.
All padding inside payload is explicit zero bytes; no trailing unowned bytes.

Instruction fields: opcode:u8 at 0, flags:u8=0 at 1, arg0:u16 at 2, arg1:u32 at 4.
Opcodes 0..72 have the enumerated values and stack behavior in
`src/vm/opcodes.h` at base commit `31f1f89d4e86a71f5375d4602bf0223d99d55965`;
`opcodes.json` freezes that mapping. New IDs are INCLUDE=73 (arg1 local file ID),
IMPORT=74 (arg1 string descriptor containing literal name@version), MAIN=75 and
DIR=76 (both operands zero). INCLUDE/IMPORT push the scope using the agreed lazy
initialization rules; MAIN/DIR push the dynamic built-in values. END finishes the
current top-level file evaluation; its caller decides program completion versus
returning the initialized scope. No compiler may renumber old opcodes.

Assignment preflight appends CHECKPROP=77 (arg1 string descriptor), CHECKINDEX=78
(arg1 index count), CHECKVAR=79 (arg1 string descriptor), and CHECKBOUND=80
(arg1 declaration descriptor, subject to the same declaration checks as BSTORE).
All have zero flags/arg0. They leave the stack unchanged: CHECKPROP observes the
top receiver, CHECKINDEX the receiver followed by its indices; CHECKVAR and
CHECKBOUND resolve the current lexical destination. They check access and
writability without reading the old value or creating a placeholder. Final stores
still revalidate. Existing IDs and the v1 layout are unchanged; older readers
reject the new unknown opcodes rather than execute them incorrectly.

Unused operands are zero. ARG must immediately precede ILOAD64/RLOAD/FUNC; these
instructions cannot be independent branch/entry targets. For ILOAD64/RLOAD the
ARG.arg1 supplies low 32 bits and the following arg1 supplies high 32 bits;
arg0 is zero. FUNC uses ARG.arg1 as body entry, arg0 as formal count, and arg1 as
the kind-2 parameter-name vector (zero when arity is zero). Jump targets are
instruction indices within the same file and function ownership; file/function entries, continuation
targets and structured-control operands must reference valid instructions.
The compiler and validator apply the opcode-specific constraints from the
existing decoder, including to unreachable instructions; runtime stack/type
errors are not all statically decidable. No instruction patching is permitted.

Data descriptor: offset:u32, byteSize:u32, kind:u32, count:u32. Nonempty blocks
occur in descriptor order, at successive 8-aligned offsets; zero padding fills
alignment gaps. Different descriptors do not alias payload. Kind 1 is a string:
count Unicode scalars followed by one zero u32, byteSize=4*(count+1). Embedded
zeros count as content. Kind 2 is count u32 descriptor indices, byteSize=4*count;
each references a kind-1 parameter name. Kind 3 is one 16-byte declaration:
tag=0x4445434c:u32, nameDescriptor:u32, file:u32, modifier:u32. Modifiers are
0=file-local, 1=public, 2=external, 3=protected, 4=private. Mutability follows the
single DVAR/DCONST declaration using this descriptor; BLOAD/BSTORE reference it.
Reject duplicate declaration instructions for a descriptor and unresolved binding
references. Names retain explicit lengths. All declaration scope/access checks
follow the language contract; a numeric ID does not confer access rights.

File record: firstInstruction, instructionCount, entryInstruction, reserved=0,
all u32. File spans partition the instruction section in files-array order;
each nonempty span contains its top-level code and own function bodies. Entry
must belong to that span and agree with JSON. A function's creation and body
entries belong to its file, and its parameter vector agrees with JSON.

Function record (eight u32): functionId, file, FUNC instruction, bodyEntry,
parameterVector (0 for zero arity), parameterCount, nameDescriptor (0xffffffff
for anonymous), reserved=0. Numeric functionId order, unique nonzero IDs; JSON
and binary tables contain exactly the same functions. Names are diagnostic only.

Location run (six u32): firstInstruction, count, file, line, column, functionId.
Runs partition instructions in increasing order; count is nonzero and every
covered instruction belongs to file. Source line/column are one-based Unicode
scalar positions; both zero designate compiler-generated locations. functionId=0
means top-level, otherwise an existing function from that file. No source text or
absolute paths are embedded. Logical filenames come from files; function names
come from the binary function record.

## Targets and native guards

`target` is the closed object in target.schema.json. OS minimums and runtime
minimums are numeric tuples of 1..4 uint32 components, compared lexicographically
after zero extension. Linux minimumOs denotes kernel version; Windows denotes
major/minor/build/revision and is at least [10]. Architecture fixes bitness:
x86=32, x86_64/aarch64=64. endian is always little. Runtime is glibc/musl on Linux,
msvcrt/ucrt on Windows; no cross-family compatibility. Runtime minimumVersion
denotes the runtime's required version; [0] states no additional version floor
above the OS/runtime-family contract, only when established by the producer.
An unknown requirement remains incompatible, never guessed satisfied.

Required initial targets are linux-x86_64-gnu, linux-aarch64-gnu,
windows-x86_64-mingw64, windows-x86_64-ucrt64, windows-x86-mingw32.
The analogous linux-*-musl names are syntactically valid but implementation is
deferred to #248. `host` resolves to a concrete target. `+feature` suffixes request
additional CPU features; base x86_64 requires SSE2, aarch64 requires FP/NEON,
x86 requires i686. Canonical feature identifiers are lowercase ASCII names:
i686, sse2, sse3, ssse3, sse4_1, sse4_2, avx, avx2, fma, bmi1, bmi2, avx512f,
fp, neon, aes, sha1, sha2, crc32, lse, sve, sve2. A reader may accept an unknown
nonempty feature identifier structurally but cannot select that variant.
For AVX-family features, usable OS register-state support is required, not merely
a CPU bit. Required feature sets include prerequisites. All requirements are
conjoined; two variants matching the same environment are an ambiguity error.

Inspect's `--target` accepts one of these IDs (baseline CPU; unspecified version
facts are unknown, not zero). For exact cross-platform checking also accept
`--target @profile.json`, whose content follows target.schema.json and describes
actual available OS/runtime versions and features. In a profile, minimumVersion
and minimumOs fields supply the corresponding available versions for comparison;
the spelling is shared with requirements to avoid a second divergent structure.
Missing knowledge is reported as unverified and prevents a positive native
availability claim. Only the current actual environment permits OS load probes.

Each specialization uses the fixed tagged-value adapter signature
`goat.adapter.v1`. `signature.arguments` and `when.argumentTypes` must match and
have the function's formal arity; result is a scalar ABI type. Missing actual
arguments are null; extra actual arguments are evaluated normally and do not add
formal slots. Guards are a conjunction of exact type checks and conditions.
Predicate argument is a zero-based formal index; its constant has the same
scalar type. equal/notEqual use scalar language equality (not bit equality for
reals); ordered comparisons are allowed only for integer/real/character. Real
NaNs obey ordinary IEEE comparison behavior. Strings compare their complete
explicit-length scalar sequence, including zeros. No user comparison overload,
callback, property access or user code is executed by a guard.

Reject detectable overlaps for a function (including equal guards and intersecting
integer intervals); runtime checks every applicable guard and fails if more than
one matches. There is no priority order. No matching guard invokes the VM with
already evaluated arguments. Unrepresentable proof requirements exclude the
specialization. These guards do not promise capture values: compiler eligibility
must be safe for all admissible captures, or use supported host-mediated reads
without assuming their value. Native code is never re-executed on the VM after
entry. ABI result-type violations are errors.

For v1's independent scalar predicates, intersection is decidable per argument:
different exact types are disjoint; conjoin both predicate lists, find a value
satisfying each argument's list, and overlap exists iff every argument has one.
Use inclusive/exclusive intervals plus excluded constants for integer/character,
finite enumeration for null/boolean, equality plus finite exclusions for strings.
For reals, handle NaN separately, coalesce the two zeros for comparisons and use
representable binary64 successors/predecessors at open bounds. The witness
algorithm in reference.guards_overlap is an executable definition for these
scalar domains; it never calls user code. Runtime ambiguity checks remain a
defensive requirement. Future guard operations require a version change.

The native-description vector is a standalone descriptor-shape/envelope example,
not a loadable DLL/SO or a claim that its illustrative mainHash names a shipped
native module. The minimal archive and two-dependency metadata/bytecode vectors
are cross-part-consistent, and the dependency hashes resolve to the supplied
math-1/math-2 metadata with the minimal bytecode image.

## Native descriptor and calling ABI

Export the data symbol `goat_module_descriptor_v1`, not a query function. It is a
contiguous byte array: 8 ASCII bytes GOATND01, 8-byte LE JSON byte length, that many
canonical UTF-8 native.schema.json bytes, then one zero byte outside the length.
No embedded pointer or wchar_t occurs in this envelope. Compare decoded descriptor
identity, target, ABI and specialization records with the selected archive entry;
the library's own hash and cCompiler are external only. Check that the entire
header and declared range lie in readable mapped regions belonging to the loaded
library before copying. Check length overflow and the terminal byte before parsing.
Never find the length by unbounded strlen. OS-loading may execute constructors;
this format is not a sandbox against malicious native code. Inspect performs
these operations in its isolated process.

Each specialization exports `goat_fn_<specializationId>_v1` with C linkage and
the target C ABI (__cdecl on 32-bit Windows). There is no arbitrary native
function-signature inference. Prototype:

```c
uint32_t adapter(uint32_t abi_version, const context *ctx,
                 uint32_t argument_count, const value *arguments, value *result);
```

ABI version argument is 1. `value` is 16 bytes, alignment 8: tag:u32 at 0,
reserved:u32=0 at 4, payload:u64 at 8. Tags: null=0, integer=1, real=2, string=3,
boolean=4, character=5. Payload holds int64 two's-complement bits, binary64 bits,
or a pointer to a borrowed string view; boolean is 0/1, character a Unicode
scalar, null zero. Unused high bits are zero. Pointer payloads are zero-extended
on 32-bit targets and never cross a process/file boundary.

Native string view has data pointer at offset 0 and uint64 length at offset 8,
size/alignment 16/8; bytes between a 32-bit pointer and length are zero padding.
Data points to immutable char32_t scalars; NULL only with length zero; no trailing
zero is required. Results are consumed/copied by the host before argument or
call-context storage expires. No VM object layout crosses this boundary.

`context` contains a services pointer then an opaque state pointer, using the
target pointer size/alignment. Services contain struct_size:u32, reserved:u32=0,
then target-sized C function pointers in this order: allocate(state,uint64)->void*,
deallocate(state,void*)->void, deallocate_all(state)->void,
is_space(uint32)->uint32, to_upper(uint32)->uint32, to_lower(uint32)->uint32.
All use the adapter's calling convention. First pointer is at offset 8. Check
the required table prefix before use. Allocations belong to the outer call,
overflow/OOM follow Goat's fatal resource policy; only the outer owner performs
bulk release. Individual releases use the same context. Character callbacks use
Unicode 17.0.0 (the fixed v1 data version). Full string case conversion must use the same pinned
tables/algorithm in the generated runtime, not a loop of simple callbacks.

Status codes: 0 success, 1 type mismatch, 2 bad request, 3 ABI mismatch,
4 resource limit, 5 external error. Any nonzero result after adapter entry is a
failure, never permission to retry via VM. Initialize result to null/zero before
entry; validate status/tag/payload/expected result type before exposing a result.
The producer exports only operations supported by this scalar boundary and its
runtime services; unsupported object/callback/capture operations stay in VM.
No claim of universal native compilation follows from format validity.

## ZIP writer profile and reader

Standard writer profile 1 uses zlib **1.3.2**, raw Deflate, level 9,
windowBits=-15, memLevel=8, Z_DEFAULT_STRATEGY, one complete entry then Z_FINISH;
no intermediate flushes or per-platform tuning. Vendor the exact release and
license in #247; its small contrib/minizip layer is the selected ZIP library,
with Goat validation around it. Builds must not download it. References:
[upstream release](https://github.com/madler/zlib/releases/tag/v1.3.2),
[license](https://zlib.net/zlib_license.html). Packager upgrades require new
writer-profile vectors; they do not change content hashes or reader format v1.

Entry order: module.json, bytecode.bin, then native filenames in unsigned ASCII
order. Writer uses canonical full normalized module JSON, with no newline.
Native variants are sorted by the normalization key and renamed consecutively
native/variant-1.dll or .so according to OS (library identity is external to main
hash). No directory entries are written. ZIP time=0, DOS date=33 (1980-01-01),
version-needed=20, made-by=0x0314, flags=0x0800, method=8, internal attributes=0,
external attributes=0100644 shifted left 16. No extra fields, data descriptors,
comments, prefix or trailing data; single disk. CRC32 and sizes are present in
both local and central headers; offsets are exact. Fail rather than use ZIP64.
`reference.zip_bytes` fixes every envelope byte and produces the golden archive.

The reader also accepts stored entries and valid Deflate/data-descriptor ZIPs,
different timestamps/attributes that still denote regular files/directories,
and optional safe directory entries. Validate local/central agreement, CRC,
declared/actual lengths, overflow, duplicate paths and ASCII case collisions;
reject encryption, ZIP64, multi-disk, links, traversal, absolute paths and
unrecognized file entries. File names are module.json, bytecode.bin and declared
flat native paths. Directory entries may only be native/ and contain no data.
Do not extract before these checks. Metadata and hashes validate every native
part, including foreign targets. A valid hash never substitutes for structure
or graph checks. No author trust is implied by CRC/SHA-256.

## Verification and implementation handoff

Run `python docs/module-format-v1/reference.py` with zlib 1.3.2 to compare all
generated files; `--write` intentionally regenerates them for review. Run
`python docs/module-format-v1/test_reference.py` for portable parser/schema/hash
and binary-vector checks. These are format tests, not proof that the existing
VM executes the new image. #219 must run the actual new reader/VM/native path on
Linux x86_64/aarch64 and Windows MINGW32/MINGW64/UCRT64 after implementation.

#178/#186 use gmod.schema.json; #180/#181 canonicalization/projection and hash
vectors; #182/#183/#247 ZIP profile and archive vector; #184/#251 bytecode tables;
#185 combines local checks; #189 adds graph resolution; #205/#231 analysis and
externals; #208 target fields; #209/#232 descriptor, ABI and guards; #233 verifies
cross-part references; #221 informational fields; #250 strings/Unicode. The
reference does not execute libraries, resolve imports, prove analysis facts or
constitute the future complete bytecode verifier.

## Standalone program envelope v1 (#251)

The standalone CLI stores the same `GOATBC01` bytes inside a `GOATPG01` envelope.
The envelope has its own version 1 and is not the future ZIP module container.
All integers are fixed-width little-endian; the header occupies 96 bytes:

| Offset | Width | Field |
| --- | --- | --- |
| 0 | 8 | ASCII GOATPG01 |
| 8 | 8 | envelopeVersion=1 |
| 16 | 8 | reserved=0 (no host ABI marker) |
| 24 | 8 | bytecode byte length |
| 32 | 8 | native binding count |
| 40,48 | 8 each | reserved=0 |
| 56 | 8 | FNV-1a checksum of the entire envelope with these eight bytes zero |
| 64 | 32 | SHA-256 of exact companion library bytes; all zero without bindings |

The checksum starts at 14695981039346656037 and multiplies by 1099511628211
modulo 2^64 after XOR with each byte. The image begins at byte 96, followed
immediately by binding records: uint64 FUNC instruction index and uint64
function ID. Both must fit the referenced v1 u32 fields and agree with the
function table. Records have strictly increasing instruction indices. No extra
bytes are allowed. Native addresses are never stored; binding only changes
external runtime structures. The standalone CLI caps artifacts at 256 MiB.
The bare codec retains the format's uint32 total-size limit.

The bytecode reader accepts only `GOATBC01` version 1; the standalone reader
accepts only this envelope or a bare new image. In particular, neither accepts
`GOATST01`/`GOATSC01` or previous numeric-version-1 layouts. Native ABI is already
version 1; historical `_v2` C identifiers are not serialized format versions.
The module's `GOATND01` descriptor and ZIP implementation remain #209/#247.

## Inspector report v1 (#223)

`inspect-report.schema.json` is the independent closed JSON Schema for
`goat-inspect --json`. Unlike archive schemas, it describes inspection results,
including errors and unavailable metadata, and does not enter any module hash.
Its complete bilingual field contract and stable code catalog are in
[the report specification](../module-inspect-report-v1.md). Native probing is
reserved for #214; current reports never fabricate a successful probe.
