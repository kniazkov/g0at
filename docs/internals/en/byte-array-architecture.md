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
str/toString/notation use uppercase #HEX, including # for empty. Preserve module
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
