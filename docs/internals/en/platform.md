# Portable support and resource ownership

[Contents](index.md) · [Русский](../ru/platform.md)

## Strings and allocation

`gchar_t` is a portable 32-bit character type, independent of `wchar_t`. Internal
lengths count those units; filesystem paths and wire text have explicit encoding
boundaries. `string_value_t` carries conditional ownership and is released with
FREE_STRING(). `string_view_t` borrows terminated storage; a bounded slice is a
different contract. Do not infer lifetime from the fact that a pointer is const.

`lib/memory` provides checked allocation, alignment and arenas. Arena objects are
released together, not individually. The core allocator's fatal-OOM policy differs
from APIs returning explicit resource errors; do not promise recoverable failure
merely because another layer has a MEMORY enum. Collection containers in
`lib/collections` have their own destruction contracts for element pointers.

## Numbers, bytes and archives

`lib/numbers` centralizes integer/bitwise semantics and number formatting;
`lib/wire` handles explicit byte representation. `lib/hash` implements SHA-256;
`lib/archive` handles ZIP. Byte order, bounds and checksum validation belong at
these boundaries. A valid ZIP CRC is not the module's significant-content identity;
module hashing and metadata rules are an additional layer.

## Filesystem operations

`lib/files/file_system.h` uses UTF-8 paths and reports a portable status plus the
native code, operation and owned path. Initialize error records and clear them.
Reading a regular file checks the opened handle; it does not accept a FIFO as an
ordinary file. Canonicalizing an existing path, joining names and validating a
bounded path are different operations.

Temporary outputs use exclusive creation and staged publication. Recursive removal
is only for an owned tree and must respect identity/link rules. A failure such as
Windows sharing violation remains an error even when the program produced correct
output; suppressing it would hide leaked handles or incomplete lifecycle cleanup.

## Processes, cleaners and threads

`lib/process/child_process.h` owns launch, capture and child outcome contracts.
Timeout, abnormal termination, a nonzero program exit and transport failure must
remain distinguishable. `cleanup_service_t` is one lazy independent cleaner per
run; native consumers share it. Its launch API transfers process-tree ownership
before child code runs, rather than registering a numeric PID afterwards.

finish_cleanup_service() is called only after resources are released/unloaded.
Its timeout does not cancel the cleaner. destroy_cleanup_service() frees client
state but does not mean the owner OS process has exited. This distinction matters
for owner-death tests and Windows loaded-library deletion.

`lib/threads` wraps OS synchronization, while language-level thread lifecycle lives
in VM/model. `lib/platform/windows_target.h` supplies platform configuration.
Avoid introducing raw platform-specific calls into language algorithms when an
existing support contract already covers the operation.

## Verify independently

Use unit tests for arithmetic, strings and collections; scenario suites for files,
process trees and cleanup. The test-only process observer pins an OS identity and
is independent of the production supervisor, so the same production bug cannot
satisfy both the action and the oracle. See [testing](testing.md).

[Common library](https://github.com/kniazkov/g0at/blob/master/docs/common-library.md) ·
[Child processes](https://github.com/kniazkov/g0at/blob/master/docs/child-process-api.md) ·
[ZIP](https://github.com/kniazkov/g0at/blob/master/docs/zip-api.md)
