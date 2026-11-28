# ByteArray values

`str(#00aAff)` and `#00aAff.toString()` return `"#00AAFF"`. The empty value renders as `#`. Each conversion creates a snapshot; later mutation does not change it. Arrays and object notation use the same uppercase hex form, which can be parsed as a Goat literal. `toString` accepts no arguments.

`==` and `!=` compare length and bytes. Other types compare unequal in either operand position, without conversion. Explicit user operator overrides retain their normal precedence. Internal collection-key comparison remains identity-based; equal byte content does not merge distinct keys. Ordering operators are not defined for ByteArray.

Empty ByteArray is false; every nonempty ByteArray is true, including `#00`. Inherited ByteArray payloads follow the same rules. Hex output checks multiplication, prefix and terminator space before allocating; overflow uses the existing fatal allocation policy.

Tests cover all 256 bytes through snapshot/parser roundtrip, nested array rendering, comparison with other types, inherited overrides, mutation, truthiness, identity comparison and size arithmetic boundaries. The language fixture runs from source and GBIN with optimization/native modes.
