# `switch`: ordered multi-way selection

```goat
const describe = func(value) {
    switch (value) {
        case 1, 2, 3,:
            return "small";
        case 4, 5:
            return "larger";
        default:
            return "other";
    }
};
println(describe(2));
```

The selector is evaluated exactly once. Case variants are arbitrary expressions, evaluated in source order and compared with the saved selector using ordinary Goat `==`. Evaluation stops at the first match: the remaining variants and all other branch bodies are skipped. This includes variants later in the same comma-separated list. A trailing comma before `:` is allowed.

A selected branch ends automatically. There is no fallthrough, including for an empty branch:

```goat
switch (value) {
    case 1:          // Matching 1 does nothing and finishes the switch.
    case 2, 3:
        println("two or three");
}
```

`default` is optional, unique, and must be last. It runs only if no case matches. With neither a match nor a default, execution continues after the switch. `switch (expression) {}` is legal and evaluates only its selector. `switch` is a statement and does not produce a value.

Each branch has an independent lexical scope, even without braces. Variants are evaluated in the enclosing scope, before entering the selected branch. Branch locals cannot be used by other branches; closures retain their captured branch scope. `this` inside a branch denotes its scope object, following ordinary Goat scope rules.

`switch` does not intercept `break` or `continue`: these still target the nearest enclosing loop. Without an enclosing loop they are invalid. Returns and exceptions from selectors, variants, or bodies keep their ordinary meaning; skipped variants cannot throw or produce side effects.

Repeated variants are legal; the first match wins. With `-w` / `--enable-warnings`, the compiler warns when it proves duplicate values by lattice evaluation of closed scalar expressions (including arithmetic, strings, booleans, characters and null). This diagnostic also works with `--optimize none`, does not prevent execution, and does not remove expression evaluation. Unknown or runtime-dependent equality is not diagnosed. Equality follows Goat rules: for example `2` equals `2.0`, but `true` does not equal `1`; NaN does not equal itself.

Native generation supports eligible pure functions with integer, real, boolean, character and string selectors/variants. It preserves lazy evaluation and exact mixed integer/real equality, including large integers; string equality includes embedded zero code units. VM-only types and operations retain the existing native eligibility restrictions. `--native required` alone does not prove every call was native, so the grouped native fixture additionally checks successful dispatch counts and zero retries against VM output.

The regular functional and grouped round-trip suites discover the `switch_*` fixtures automatically. `python3 scripts/check_native.py build/goat build/switch-native --only switch` runs the native comparison. The parser tests cover invalid labels/default placement, nesting and loop-transfer boundaries.

## Русский

Значение `switch` вычисляется ровно один раз. Варианты `case` — произвольные выражения, вычисляемые по порядку до первого совпадения по обычному `==`. После совпадения оставшиеся варианты, включая варианты той же ветки, не вычисляются. Варианты разделяются запятыми; последняя запятая перед `:` допустима.

Выбранная ветка завершается автоматически. Проваливания нет, в том числе у пустых веток. `default` необязателен, может быть только один и только последним. При отсутствии совпадения и `default` выполнение продолжается после `switch`. Пустой `switch (expression) {}` вычисляет выражение, но ничего не выбирает. Это оператор, а не выражение с результатом.

Каждая ветка имеет собственную область видимости. Выражения вариантов вычисляются во внешней области; локальные переменные ветки видны только внутри неё. Замыкания сохраняют область ветки. `this` обозначает объект этой области по обычным правилам Goat.

`break` и `continue` относятся только к ближайшему циклу, даже если находятся внутри `switch`. Без цикла это ошибка. `return` и исключения работают и в селекторе, и в вариантах, и в телах веток. Пропущенные варианты не вызывают побочных эффектов и не выбрасывают исключения.

Повторы разрешены: выигрывает первое совпадение. Флаг `-w` включает неблокирующие предупреждения о повторах, доказанных вычислением замкнутых скалярных выражений в решётке. Предупреждения работают и без оптимизации. Для значений, зависящих от выполнения, совпадение заранее не предполагается.

Нативная реализация поддерживает подходящие чистые функции с числами, булевыми значениями, символами и строками. Тесты сравнивают вывод VM и нативного кода, проверяют реальные нативные вызовы, восстановление исходника и выполнение сохранённого байткода.
