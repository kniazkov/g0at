# 5. Lexical Analysis

[Contents](index.md) · [Русский](../ru/05-lexical-analysis.md) · [Previous chapter](04-implementation-in-c.md) · [Next chapter](06-syntax-analysis.md)

Edition 2. Implementation described: [commit 8d1fe86, including `println`](https://github.com/kniazkov/g0at/tree/8d1fe867ff5272d8d59a44785871f0db1b454df0).

<a id="section-5-1"></a>

## 5.1. Where Text Ends and a Program Begins

In `var count = 12;`, a person immediately distinguishes a declaration, a name, an assignment, and a number. The scanner receives only a sequence of characters. Its job is to identify tokens (elements of the notation that later parsing can handle as units) and retain each element's source location.

The main implementation is in [scanner.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/scanner/scanner.c). [scanner_t](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/scanner/scanner.h) holds the working text, current position, arenas, and token groups. `get_token` returns the next token, `NULL` at the end of the text, or a `TOKEN_ERROR` token when recognition fails. The complete program tree has not yet been built.

<a id="section-5-2"></a>

## 5.2. Encoding, a Working Copy, and Coordinates

[File reading](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/lib/io.c) uses [UTF-8 decoding](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/lib/string_ext.c). The result is stored in `wchar_t` units: on platforms with 16-bit `wchar_t`, a supplementary Unicode character may occupy two units. The internal string length therefore need not equal the number of visible characters, much less the file's byte count.

The scanner creates its own text copy in the token arena and preprocesses it by replacing carriage returns `\r` and comments with spaces. `get_token` then skips whitespace. A newline does not become a separate delimiter token.

A [position](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/common/position.h) contains the filename, row, column, and offset. Rows and columns start at `1`. Crossing `\n` increments the row and resets the column to `1`; a tab adds four columns rather than advancing to the next tab stop. The offset increases by one internal text unit per step.

A token range records its start and the end after its last accepted character. For example, in `  test`, the name starts at column `3` and ends before column `7`. Numeric coordinates are stored separately from tokens and can be used later. The `code` pointer in a full position refers to the scanner's working copy; it does not acquire a longer lifetime merely because the position structure lives in another arena.

<a id="section-5-3"></a>

## 5.3. Names, Keywords, and Operators

A name begins with a character accepted by `is_letter` and continues with such characters or digits. The implementation uses `_`, Latin letters, and explicitly listed code ranges, including Greek and Cyrillic.

> [!CAUTION]
> This is a custom range check, not a complete implementation of Unicode identifier rules; some ranges contain more than letters.

After reading a complete name, the scanner compares it with the keyword table. `var` becomes `TOKEN_VAR`, while `variable` remains an identifier. `println` is also an ordinary identifier: binding and analysis identify the built-in function, not the lexer. The words `null`, `true`, and `false` immediately receive corresponding literal nodes.

Operators come from an explicit table. The scanner checks whether two adjacent characters form a known two-character operator; otherwise it accepts one character. For example, `<=` is one token, whereas `+-` is two. In `-12`, the sign is recognized separately from `12`; the parser selects unary negation (an operation with one operand).

Commas, semicolons, and three bracket kinds are also recognized: `()`, `{}`, `[]`. Recognizing a bracket does not establish support for a language construct.

> [!CAUTION]
> In this revision, scanner-level support for `[]` does not provide array syntax, and a dot `.` outside a number is not a property-access operator.

<a id="section-5-4"></a>

## 5.4. A Literal Has Both Spelling and Value

A literal (a value written directly in source code) may receive an AST node during scanning. Integer digits accumulate in `uint64_t` and are then mapped to a signed representation. There is no integer-literal range check: accumulation wraps modulo \(2^{64}\).

A dot or `e`/`E` selects the real-number case. Conversion uses `wcstod`; ordinary examples are `2.5`, `2e3`, and `2.5e+1`. A number must start with a digit, so `.5` is not such a literal.

> [!CAUTION]
> This algorithm does not introduce hexadecimal notation or digit separators.

A string starts and ends with a double quote. Supported escape sequences (a special character written using a backslash) are `\n`, `\r`, `\t`, `\b`, `\\`, `\'`, and `\"`. An unknown sequence or an unclosed quote produces an error. Token text and string-node value differ: `"A\tB"` is spelled with a backslash and `t`, while the node contains an actual tab. The string builder is temporary; the created node copies the required data into the graph arena.

[Token example](../examples/05-tokens.goat):

```goat
// Names, literals, and operators.
var число = 12;
const scale = 2.5e+1;
println(число + scale);
println("A\tB");
```

The first output line is `37.0`. The second contains `A`, a tab character, and `B`; the visible spacing depends on the terminal. The Cyrillic name is accepted here, and `scale` has the real value `25.0`.

Run it on Linux:

```sh
./goat --native off docs/book/examples/05-tokens.goat
```

In Windows PowerShell:

```powershell
.\goat.exe --native off .\docs\book\examples\05-tokens.goat
```

<a id="section-5-5"></a>

## 5.5. One Token Belongs to Two Lists

[token_t](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/scanner/token.h) stores a kind, text, coordinates, an optional AST node, and a child-token list. It also has two independent sets of links:

| Links | Purpose |
|---|---|
| `neighbors`, `left`, `right` | Neighbors in the current source sequence |
| `group`, `previous_in_group`, `next_in_group` | Members of one parsing-rule group |
| `children` | Bracket-pair contents after grouping |
| `node` | The node created for a literal or reduced construct |

Suppose the source contains two additions. The neighbor list shows the operands surrounding each `+`; the additive-operator group lets the parser visit the relevant signs without scanning all the text again.

A group is a working index for the parser, not a final semantic classification. For example, `!` and `~` initially join the same group as `+` and `-` because the unary-operation pass processes that group. The order and meaning of subsequent passes are specified separately.

[List operations](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/scanner/token_list.c) update neighboring links, the first and last elements, and the count. Removing a token from a list does not free its memory: the arena owns it. This allows temporary containers to survive for deferred parsing of their contents.

<a id="section-5-6"></a>

## 5.6. Limitations Visible in the Implementation

> [!CAUTION]
> Comment preprocessing runs before string parsing and does not track quotes. Thus `/*x*/` inside a string is also replaced with spaces. The source `println("a/*x*/b");` produces `a`, five spaces, and `b`, rather than the original comment text. `//` inside a string can remove the remainder, including its closing quote. This is a defect in the current processing, not a recommended way to write strings.

> [!CAUTION]
> Block comments have two further limitations: their embedded newlines are replaced with spaces too, so subsequent row numbers may decrease; an unclosed `/*` consumes the rest of the text without a separate comment error. For example, `println(1); /* unfinished` still prints `1`. These properties matter when reading this revision's diagnostics.

> [!CAUTION]
> Real-number recognition is not a complete validation of literal grammar either. After `e` and an optional sign, the code does not require a digit or check where `wcstod` stopped. In the verified example, `println(1e+);` is accepted and prints `1.0`. This input should be understood as a validation gap, not a promised numeric notation.

> [!CAUTION]
> Finally, the scanner puts lexical-error text in `TOKEN_ERROR`, but the bracket-grouping path in [parser.c](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/parser/parser.c) does not carry that text into the compilation message. An unknown symbol can therefore produce coordinates with an empty explanation. The book records actual behavior, including this incomplete diagnostic handling.

<a id="section-5-7"></a>

## 5.7. What Existing Tests Check

[Scanner tests](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/test/test_scanner.c) check identifiers, operators, literals, and coordinates. [String tests](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/test/test_string_ext.c) and [input/output tests](https://github.com/kniazkov/g0at/blob/8d1fe867ff5272d8d59a44785871f0db1b454df0/src/test/test_lib_safety.c) check the underlying text conversions. These are distinct levels: a correct UTF-8 decoder does not by itself prove correct comment handling or a complete set of accepted names.

Scanner output already provides material for the next stage: elementary values, element categories, and coordinates. It does not yet determine what `+` means between these elements, which construct an `else` belongs to, or where call arguments end. Those decisions belong to syntax analysis.
