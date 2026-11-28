# ByteArray architecture (BA-01)

Status: implementation decision for #559; ByteArray is not implemented by this document.
Contract: [#558](https://github.com/kniazkov/g0at/issues/558).
Audit baseline: 4bd2c2c5205cb16bb3b3162ef6afcc1cfd3f6705 (2026-11-27).

## Representation and construction

Use TYPE_USER_DEFINED_OBJECT with an optional, exclusively owned byte-array payload.
Do not introduce a separate object type for literals. A plain instance has the
builtin ByteArray prototype, an own readonly length cell and payload
{ uint8_t *data; size_t length; size_t capacity; }. No object per byte.
The immutable builtin prototype is a singleton without mutable payload.
ByteArray recognition uses own payload presence, never inherited storage or a
name/property test. An uninitialized descendant is not a valid method receiver.

The literal factory uses the builtin singleton directly, creates the same object
representation and initializes its storage, then copies the constant bytes.
It neither looks up the identifier ByteArray nor executes user-overridden init.
Ordinary inheritance continues to use the existing constructor chain.
Repeated init on an initialized receiver is an idempotent return of that receiver,
without discarding bytes. Validate arity first. A diamond gets one owned buffer,
even if repeated inherited lookup reaches the same builtin initializer.

Check for an own length cell before allocating storage; conflict raises
PROPERTY_ALREADY_EXISTS. Mixed ByteArray/StringBuilder construction is rejected
by this same exclusive-length rule in either constructor order. Do not silently
share length, drop a payload or change prototype traversal priority. An inherited
length does not prevent a child obtaining its own empty buffer. A later user
declaration conflicting with the builtin length follows ordinary field rules.

Construction failure does not roll back arbitrary user side effects. If user init
has published the receiver and then throws, that object remains valid and owned;
if unreachable it is reclaimed normally. Cleanup must handle every partial state.

## Copying, dispatch and observations

Assignment aliases; clone copies own byte storage independently, while preserving
existing prototype/field/access-domain and transfer rules (not a new deep clone of
all referenced fields). flat retains existing flatten rules and copies only the
source's own payload, never an ancestor's buffer. Payload recognition therefore
continues to work after flat removes the ByteArray prototype. Copy builtin methods
through the existing flatten machinery; test their accessibility explicitly.
slice always constructs a plain ByteArray and copies only selected bytes.

Keep ordinary left-operand operator lookup and explicit user overrides. Provide
builtin []/[]=, <<, ==/!= handlers and payload-aware model fallbacks for C/model
paths. No +, += or ordered comparison support is introduced. Common observation
and truthiness must recognize own payload even in flattened objects: empty is
false despite the length field, nonempty is true. Builtin equality compares bytes
and length, including descendants; other types compare false/true for ==/!=.
Explicit user operator overrides still obey the existing language rules.

Keep address-based internal object comparison/key identity. Mutable content must
not reorder AVL keys; value equality is separate from that comparator.
str/toString/notation use uppercase `#HEX`, including # for empty. Preserve module
observation/access errors; do not bypass them through a payload fast path.

Errors: invalid byte/range/type/arity is INVALID_ARGUMENT; a method with an
uninitialized/unrelated receiver is INVALID_OPERATION. Mutation of the builtin
singleton raises IMMUTABLE_OBJECT, including empty/no-op operations. Field
conflicts retain PROPERTY_ALREADY_EXISTS. All argument checks precede mutation;
resize validates fill even on shrink/no-op.

## Ownership and concurrency

| Resource | Owner / roots | Release / invalidation |
|---|---|---|
| Object and own length cell | Existing user-object refs and GC edges | Existing iterative release/sweep |
| Byte payload and capacity | Exactly one user object; raw bytes have no GC edges | clear_fields before pooling, failed unreachable init, destruction |
| Literal bytes | Immutable bytecode image | Image lifetime; never borrowed by mutable result |
| clone/flat/slice payload | New result object | Independently freed |
| Temporary result across safepoint | Published stack/root handle, not only INCREF | Existing root protocol |
| C byte view | Borrowed from rooted owner under execution lock | Ends before any call, suspension, mutation or lock release |

Extend gc_owned_payload by the storage allocation and actual data allocation
size; note growth using gc_policy_object_bytes/gc_policy_note_growth. Do not count
byte values as graph edges. Free the payload before returning the shell to its
pool; clear() on a live object intentionally retains capacity.
All operations run under the existing process execution protocol. Observation may
wait: root operands, observe first, then reacquire data and validate lengths.
Do not carry a raw pointer across observation, allocation that permits suspension,
callbacks or VM/native transitions. Rooting protects object lifetime, not pointer
stability or concurrent contents. Copy to owned scratch storage before future
blocking I/O; pinning and async zero-copy require a separate design, not this API.
Self append must preserve offsets across realloc; copyTo uses memmove semantics.

Runtime maximum length/capacity is min(SIZE_MAX, INT64_MAX), additionally subject
to allocator representability; use subtraction-based range checks and checked
growth/format-size arithmetic. Unrepresentable requests raise INVALID_ARGUMENT
before mutation. Wire limits are stricter and checked separately on export:
no truncation or artificial uint32 runtime cap. Allocation failure retains the
current fatal OOM contract; no implicit GC/retry inside ALLOC.

## Formats, analysis and native compatibility

Add a distinct raw-byte descriptor and load instruction in #565. Payload size
equals byte count, without terminator/endian conversion; alignment padding is
outside the payload. Existing opcode values remain unchanged. Each evaluation
copies into a fresh object, including identical/deduplicated constants.
The current reader rejects unknown kinds/opcodes; the linker currently treats
non-string blocks as u32. Both must change atomically with emission.

#565 must record the compatibility decision and executable old/new-reader matrix
before emitting the extension: either an explicitly versioned wire extension or
documented existing-version reject-on-unknown extension. This is a format gate,
not permission to silently repurpose fields. Container/schema versions are
independent; #569 changes only affected layers and generated schemas from their
source. No new public scalar native ABI tag or raw buffer pointer is introduced
by the initial implementation. Unsupported native candidates fall back BEFORE
invocation; a failure after invocation never retries in VM.

Initially use conservative user-object/TOP analysis and unknown mutation effects;
never infer constant contents from an immutable literal image. #663 is the
publication gate for #561/#565, preceding refined analysis #568 and native #570.
It may land atomically with runtime integration to make its tests executable.
Metadata can conservatively erase ByteArray precision, never claim object Array
storage. #569 owns exact exported type/version decisions.

## Evidence and implementation map

Paths below are relative to the repository root; inspect their audited functions.

| Source | Evidence / required work |
|---|---|
| src/model/user_defined_object.c | optional StringBuilder payload, clear_fields, clone_in_module, flat_object_with_access, gc_owned_payload; add parallel ByteArray lifecycle and observations (#561) |
| src/model/string_builder.c | init owns length; observation precedes reacquiring buffer; use existing pattern without changing StringBuilder semantics (#561/#562) |
| src/model/init_snapshot.c | identity-deduplicated traversal, rooted receiver/function snapshot; preserve ordering (#561) |
| src/model/operator.c | lookup applies to user objects only; preserve overrides (#561/#566) |
| src/model/common_methods.c | language comparison distinct from address-key comparator (#566) |
| src/model/array_internal.h | object_t ** elements unsuitable for byte storage (#560) |
| src/model/root_context.c | register builtin prototype (#561) |
| src/vm/bytecode.h, src/vm/image.c, src/codegen/linker.c | descriptors, validation and u32 serialization boundary (#565) |
| src/analysis/lattice.h, src/analysis/module_interface.c | conservative effects/types and metadata (#663/#568/#569) |
| src/module/module_abi.h | invoke-once, no fallback after failure (#570) |
| lib/strings/string_ext.h | reuse encode_utf8_view/decode_utf8_n preserving NUL (#567) |

GC #543/#550/#552 are closed; their root/pool/protocol contracts already apply.
#554 remains open, but its fatal allocation policy does not block byte storage.
Read memory-management.md, gc-allocation-policy.md and gc-fatal-oom.md in this
directory when implementing. No dependency on completing unrelated GC diagnostics.

## Verification obligations

This documentation-only step adds no runtime claims or ByteArray tests.
#560 tests zero-size/capacity/overflow; #561 tests repeated init, diamond,
both mixed-payload orders, length conflicts, escaped failed init, clone/flat and
GC/pool reuse. #562/#563 test alias/self-append/overlap and no partial mutation.
#566 tests empty truthiness despite own fields, symmetric builtin equality,
explicit overrides, stable keys and notation. #565/#569 test wire limits,
malformed inputs, immutable images and fresh literals in saved modules.
#663/#568/#570 test mutation invalidation and invoke-once fallback.
#571 links every contract clause to passing tests. New failures require issues
and checklist updates; architecture acceptance is not implementation completion.

## Byte storage API (BA-02)

`lib/bytes/byte_buffer.h` implements storage only, independently of VM objects.
Initialize with `{0}`; exclusive ownership forbids shallow copies. Copy into an
initialized destination with byte_buffer_copy. Destroy resets all fields and is
repeatable. Resize fills only new bytes; clear retains capacity; shrink frees an
empty buffer. Reserve grows geometrically. Append accepts a buffer plus offsets,
so self-append reacquires storage after growth. Copy-range uses memmove and does
not resize. Distinct owning buffers must never share allocations.

False means an invalid size/range with no mutation; ALLOC failure is still fatal.
C callers supply uint8_t fill: Goat argument validation belongs to BA-03/BA-04.
The limit includes Integer and allocator-header constraints. Capacity arithmetic
can be tested without allocation. owned_bytes reports only the dynamic data;
the future object GC callback must also account for separately allocated storage
and call the existing growth notification. This layer neither roots objects nor
invokes GC. All views expire on storage replacement and require external locking.

CTest `core.byte_buffer` covers lifecycle/accounting, zero and maximum sizes,
copy independence, overlap, self-append with/without replacement and repeated
geometric growth. Run it in Debug, Release and ASan/UBSan builds.

## Object integration (BA-03)

ByteArray is now a root builtin. `ByteArray -> {}` creates an own empty payload
and readonly length; descendants are empty, repeated init preserves storage,
and clone/flat copy the source's own bytes. Mixed StringBuilder construction
fails on the own length conflict. Byte payloads are freed before shell pooling;
GC accounts for storage and byte capacity. Empty truthiness is already supported.

This step exposes construction and strict indexed access only. Populating a
buffer in the model test is a C fixture, not a language constructor. Append,
resize, byte literals, byte equality and canonical notation remain later tasks.
A plain ByteArray therefore cannot yet be populated from Goat code.

The ordinary user-object vtable delegates byte indexing to the payload handlers.
The shared property-index predicate keeps module waiting, access checks and
own-this writes for non-byte user objects; it excludes byte payloads, whose
indices are strict Integer positions. Explicit user operator lookup still runs
first. Tests must preserve private/property semantics on ordinary objects.

Initial admission for #663 uses existing conservative mechanisms: lattice_truth
returns ABSTRACT_EITHER for user objects; index_access invalidates mutable facts
and its native C expression path supports only scalar argument slots. Builtin
init has bindings effects and TOP result. module_native convert_argument rejects
user objects before ABI invocation; no ByteArray tag is added. The existing
invoke-once error protocol is unchanged. Native mode tests include a scalar seed
with proven native entry and ByteArray operations via the supported fallback.
The literal/opcode portion of #663 remains a gate for #565, not completed here.

`core.byte_array` covers populated C fixtures, invalid indices/bytes, clone/flat,
GC roots, cycle disposal and pool reuse. `native.byte_array` compares source and
saved programs under optimization none/all and native off/auto/required, verifies
actual scalar native entry and hashes the saved image before and after execution.
The functional fixture covers diamonds, readonly fields, mixed init conflicts,
escaped receivers after constructor failure and ordinary property indexing.

## Mutation API (BA-04)

The BA-03 population restriction above is historical: the following methods now
populate ByteArray from Goat. Every successful operation returns the receiver.

| Operation | Contract |
|---|---|
| append(byte) | Strict Integer 0..255 |
| append(other, start = 0, count = remainder) | Copy ByteArray range; range arguments require ByteArray |
| receiver << value | append(byte/ByteArray), chainable |
| resize(length, fill = 0) | Strict nonnegative Integer length; validate fill even on shrink/no-op |
| clear() | Zero length, retain capacity |
| shrinkToFit() | Capacity becomes length; empty releases data |

All argument/range/overflow checks precede mutation and return INVALID_ARGUMENT
on failure. Builtin prototype mutation raises IMMUTABLE_OBJECT, including no-op.
There is no new + or += support. Shrink discards bytes; regrowth fills them anew.
Append to self uses offsets and reacquires the source after storage replacement.
Observation of the source happens before acquiring buffers/range validation,
since module observation can suspend. Length is refreshed after success and
owned-byte growth is reported to the existing GC policy. Effects remain
conservative bindings/TOP; no mutable byte contents are folded into constants.

The mutation fixture checks chained alias returns, errors without data loss,
inheritance, clone/flat, immutable prototype and absence of +/+= support in the
source/GBIN and VM/native matrix. C tests additionally assert capacities, exact
receiver identity and self-append with and without storage replacement.

## Byte ranges (BA-05)

| Method | Result and bounds |
|---|---|
| slice(start, count = remainder) | New plain ByteArray, independent copied bytes; no descendant fields/prototype |
| copyTo(target, sourceStart, targetStart, count) | All four arguments required; returns target, never resizes it |
| fill(byte, start = 0, count = remainder) | Returns receiver, fills existing bytes only |
| indexOf(byteOrArray, start = 0) | First match at/after start, or -1 |

Indices/counts are strict nonnegative Integer values. All ranges are validated
using subtraction before writes. Zero count allows start == length, but never
start > length. Invalid arguments raise INVALID_ARGUMENT without changing either
buffer. Mutating the builtin prototype, including using it as copyTo target,
raises IMMUTABLE_OBJECT. copyTo supports self/alias overlap with memmove semantics.
An empty search pattern matches at start (including length); a nonempty pattern
at length is not found. Byte values are strictly 0..255, without conversion.
The initial subsequence search is a straightforward bounded byte comparison.

copyTo/indexOf observe the other object before obtaining borrowed buffers, because
observation can suspend. slice uses the internal builtin factory and copies raw
bytes, never user fields or init overrides. Its new allocation is accounted by
the normal GC growth mechanism. All methods retain conservative builtin effects.

The ranges fixture runs in the source/GBIN and VM/native matrix and checks every
valid overlap of a six-byte buffer against an independent positional oracle.
Error tests compare both source and destination with snapshots, including empty
ranges, wrong types/arity and oversized counts. Model tests additionally inspect
the direct slice prototype, independent allocation and liveness across GC.

The slice factory uses the caller module and file access context, so its public length remains accessible in source and loaded programs.

## Byte literal frontend (BA-06)

The scanner/parser and graph accept `#` followed by an even number of ASCII hex
digits (including zero). Examples: `#`, `#00`, `#aAbB`. Canonical graph output is
uppercase, two digits per byte. Storage is an arena-owned raw byte block.
Like other literals, the token uses TOKEN_EXPRESSION; NODE_BYTE_ARRAY supplies
its distinct identity, with no lookup of the identifier ByteArray.

Whitespace, operators and `{ } ( ) [ ] , : ;` delimit the token; comments follow
the scanner's normal whitespace preprocessing. Any other contiguous suffix,
including identifiers, non-ASCII letters, underscore or another hash, makes the
whole candidate invalid. Odd hex length is also invalid. No whitespace is allowed
inside one literal. Source ranges include the hash and full invalid candidate.

Execution is intentionally disabled until #565. analyze rejects the node before
optimization (also in unreachable code), with a localized BA-07 diagnostic;
source, saved compilation and module compilation cannot produce a fake value.
Calling its bytecode generator without analysis is an internal invariant failure.
#565 must remove this gate only together with validated raw wire storage and
fresh mutable runtime instances. The frontend roundtrip test is core.byte_literal.

## BA-07: raw payload

Raw byte descriptors use kind 4 (DATA_BYTES), with size == count in bytes,
no terminator or endian conversion. Offsets retain eight-byte alignment and
inter-descriptor padding must be zero. Empty payloads are legal. This is a
reject-on-unknown extension of wire v1: existing descriptor/opcode numbers and
old images remain unchanged; old readers reject kind 4. Container and ABI
versions are unchanged. No frontend emission or execution is enabled in this
first BA-07 step; opcode, fresh instances and native admission remain pending.

## BA-07: BYTELOAD

BYTELOAD (81) requires flags=arg0=0 and arg1 referencing DATA_BYTES. It pushes a fresh mutable ByteArray with copied bytes and caller module/file ownership. Existing opcode numbers stay unchanged; older readers reject 81. Disassembly prints the descriptor index. Frontend execution is now enabled. Literal nodes are not pure and cannot generate native C directly; native code may coexist with VM literal execution. Tests cover source/GBIN and optimization/native modes. Remaining BA-07 acceptance includes independent golden vectors and image-lifetime stress.

## BA-07: image ownership acceptance

core.byte_image uses a hand-authored 147-byte wire image (BYTELOAD 0; THROW; END) and checks linker output byte-for-byte. Two borrowed wrappers execute in distinct processes, and result mutation cannot change the shared image or the other result. The wrappers and owning image are then released before inspecting and growing the result buffers. THROW is only the host test transport, retaining the result in the thread exception slot. Every truncated prefix and corrupt opcode, flags, operands, descriptor offset/size/kind/count is rejected. The final allocation balance must match the baseline.

## ByteArray admission regression

The admission fixture checks ByteArray alias changes to length, bytes and truthiness across calls; exact side-effect counts through explicit and invalid-byte exceptions; independent literal allocations; and ordinary object/Array/StringBuilder controls. It runs through the existing source/GBIN, optimize none/all and native off/auto/required matrix. The numeric seed proves native execution is enabled, not native specialization of ByteArray. BYTELOAD nodes remain non-pure and cannot directly lower to C; receiver proof rejects unlisted opcodes. function.c terminates after native exceptions, with resource retries restricted to the existing safe contract. Detailed native specialization and alias precision remain BA-10/BA-12.
