# Universal C library

The `goat_common` static target (`build/libcommon.a`) owns all former `src/lib`
implementations. It has no include or link dependency on `src`, `core`, the
compiler, or the VM. Consumers link the target instead of recompiling utility
sources. The public `include/goat/unicode.h` scalar contract is shared with the
native ABI and remains in its existing location.

## Source and dependency audit (#322)

Each file from `src/lib` was checked for includes and external symbols. All were
universal: `value_t` contains primitive values and pointers, not Goat objects;
allocation accounting, process exit and cleanup contain no VM policy. Therefore
no implementation needed to stay in `src/lib`. Headers remain beside sources.

| Directory | Former files (stems, including platform/internal variants) |
| --- | --- |
| `lib/memory` | allocate, alignment, arena |
| `lib/collections` | avl_tree, comparison, linked_list, pair, queue, value, vector |
| `lib/numbers` | bitwise, integer_math, number_format, split64 |
| `lib/strings` | string_ext, unicode, unicode_data.h |
| `lib/files` | binary_file, file_system, io, path |
| `lib/process` | child_process, cleanup_service, process_channel, process_exit, windows_command_line |
| `lib/threads` | os_thread |
| `lib/platform` | windows_target |
| `lib/hash` | sha256 |
| `lib/archive` | zip_archive |
| `lib/wire` | wire |

Cross-topic includes use `lib/<topic>/<name>.h`. Sources use the CRT/OS and these
utility interfaces; ZIP additionally uses the bundled minizip/zlib targets.
Linux thread and Windows bcrypt/advapi32/shell32 dependencies are transitive.
`sha256.c` is no longer compiled into `goat_module`, and the separate `goat_zip`
archive has been removed. All common objects have a single owner.

## Build contracts

- `POSITION_INDEPENDENT_CODE` is enabled for the shared utility archive.
- Non-Release `MEMORY_DEBUG` is a PUBLIC compile definition on `goat_common`,
  keeping allocator signatures identical for the library and its consumers.
- Dedicated allocation-failure builds instrument common objects using the same
  root-level hook as other targets. Production builds expose no failure knob.
- `cleanup_service_testing` links only `goat_common` (and system dependencies),
  not `core`; existing cleanup tests exercise this standalone boundary. ZIP and
  module metadata hosts likewise require no VM objects.
- Special cleanup/process-exit test implementations remain explicit test objects;
  archive extraction does not pull in duplicate definitions.
- `goat_native` deliberately compiles only native strings plus number formatting
  and Unicode. It does **not** link `goat_common` or inherit its allocator/debug
  definitions. The same source subset is embedded for target-specific native
  compilation. Native archive contents and allocator independence are unchanged.

The Unicode table retains its `.inc` name in this step. Renaming it is #323.
No language/module formats or utility API contracts change in this relocation.
