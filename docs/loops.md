# Loops and loop transfers

```goat
var n = 0;
while (n < 3) {
    println(n);
    n++;
}
do {
    n--;
} while (n > 0);
for (var i = 0; i < 10; i++) {
    if (i == 2) continue;
    if (i == 5) break;
    println(i);
}
```

`while (condition) statement` tests the condition before each iteration and may execute zero times. `do statement while (condition)` executes its body at least once. Conditions use ordinary Goat truth conversion. The semicolon after `do…while` is optional. A body can be a block, a single statement, or `;`.

`break` leaves the nearest enclosing loop. `continue` starts its next iteration: it tests the condition in `while`, executes the step and then tests the condition in `for`, and tests the condition in `do…while`. Neither keyword takes a value or a label.

Both transfers are valid only inside a loop body, including nested blocks and `try/catch`. A function starts a new boundary: a function declared inside a loop cannot break or continue its caller's loop. Transfers in a loop's initializer, condition, or step are rejected, unless they belong to a separate loop nested there.

Each iteration has its own body scope. Closures keep captured iteration locals alive. A transfer unwinds intermediate scopes, exception handlers, and temporary expression values. It preserves the enclosing loop's scope on `continue`; leaving the loop releases that scope as well. `return` and `throw` retain their ordinary behavior.

All three loops and both transfers work in VM bytecode and in eligible native functions. Native lowering supports existing scalar types, including strings, with the usual purity and type-proof requirements. `try/catch` and object/array operations still use the VM. `--native required` only requires a native binding globally; the grouped native tests additionally verify actual native calls and compare their output with VM output.

Run `build/loop_index_testing native.index build/goat build test/data` for scope/control-flow fixtures, including source and bytecode round trips at both optimization levels. Run `python3 scripts/check_native.py build/goat build/native-check --only loops` for the native comparisons. The regular functional suite and CI discover these fixtures through their existing lists.

## Циклы и переходы

`while (условие) оператор` проверяет условие до каждой итерации, поэтому тело может не выполниться ни разу. `do оператор while (условие)` сначала выполняет тело, затем проверяет условие. Точка с запятой после `do…while` необязательна. Тело — блок, отдельный оператор или `;`. Условие преобразуется в логическое значение по обычным правилам Goat.

`break` выходит из ближайшего цикла. `continue` переходит к следующей итерации: у `while` — к условию, у `for` — сначала к шагу, затем к условию, у `do…while` — к условию после тела. Значений и меток у этих операторов нет.

Переходы допустимы внутри тела цикла, в том числе во вложенных блоках и `try/catch`, но не через границу функции. В заголовке цикла переходы запрещены, кроме переходов собственного вложенного цикла. Для каждой итерации создаётся отдельная область тела; замыкания сохраняют захваченные локальные значения. При переходе освобождаются промежуточные контексты и временные значения выражений; контекст самого цикла сохраняется при `continue` и освобождается при выходе.

Циклы и переходы поддержаны и в VM, и в нативном коде для функций, удовлетворяющих существующим требованиям чистоты и доказуемости типов. Строки тоже поддержаны. Исключения, объекты и массивы пока сохраняют существующие ограничения нативного выполнения. Тесты сравнивают VM и нативный результат, проверяют число нативных вызовов, оптимизированное и неоптимизированное выполнение, восстановленный исходник и сохранённый байткод.
