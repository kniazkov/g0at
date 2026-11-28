# Explicit UTF-8 conversion

`"Привет".toUtf8()` returns a fresh mutable ByteArray containing `#D09FD180D0B8D0B2D0B5D182`. It accepts no arguments and requires a String receiver. Each call owns independent storage.

`String.fromUtf8(bytes)` accepts exactly one ByteArray (including a descendant with its own byte payload) and returns a String snapshot. The receiver must be the String prototype. Empty input returns an empty string; embedded NUL, BOM (U+FEFF), and non-BMP scalars are preserved. No BOM is added or removed. Mutating the bytes after decoding cannot change the string.

Invalid UTF-8, argument types or arity raise `INVALID_ARGUMENT`. Wrong or detached receivers raise `INVALID_OPERATION`. Invalid UTF-8 includes overlong sequences, lone continuation bytes, surrogate encodings, truncated sequences and values beyond U+10FFFF. No replacement characters or implicit conversions are used.

`str(bytes)` still returns the canonical hex literal, not decoded text. Encoding/decoding reuse the length-aware common Unicode codec, independent of locale and wchar_t width. Temporary encoder storage is freed after copying; decoder-owned text passes to the String object. ByteArray observation retains module failure/access handling. Conservative builtin effects prevent caching mutable byte input; native scalar ABI is unchanged. Overflow/OOM retains the common library's fatal allocation policy.
