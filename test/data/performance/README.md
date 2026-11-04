# Native performance check / Проверка нативной производительности

The benchmark compares VM execution with native execution of a precompiled integer call tree. Build Goat in Release mode first. Run these commands from the repository root.

Бенчмарк сравнивает исполнение заранее скомпилированного дерева целочисленных вызовов в VM и нативном режиме. Сначала соберите Goat в Release. Выполняйте команды из корня репозитория.

## Linux

```bash
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc
cmake --build build/release --target native_performance_scenarios
ctest --test-dir build/release --output-on-failure -R '^integration.native_performance.benchmark$'
```

## Windows (PowerShell)

```powershell
cmake -S . -B build/release -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=gcc
cmake --build build/release --target native_performance_scenarios
ctest --test-dir build/release --output-on-failure -R '^integration.native_performance.benchmark$'
```

The C runner does not require Python. CTest selects the configured compiler through `CC`, runs this Release benchmark serially, and retains a unique `scenario-*` directory below `build/release/ctest/native performance/`. It contains generated source, compiled programs, the native library, dispatch reports, and `timings.json`.

C-runner не требует Python. CTest выбирает настроенный компилятор через `CC`, запускает этот Release-бенчмарк последовательно и сохраняет уникальный каталог `scenario-*` внутри `build/release/ctest/native performance/`. В нём находятся сгенерированный исходный текст, скомпилированные программы, нативная библиотека, отчёты о диспетчеризации и `timings.json`.

Both variants are compiled before timing. Each gets a warm-up and five measured process runs. The check verifies results and actual native entry, and passes only when the native median is strictly lower than the VM median. This measures the whole execution process; it is not a general speedup claim for Goat programs. Host contention can affect the result.

Оба варианта компилируются до измерения. Для каждого выполняются прогрев и пять измеряемых запусков процесса. Проверка сверяет результаты и фактический вход в нативный код и проходит только при медиане нативного времени строго ниже медианы VM. Измеряется весь процесс исполнения; это не утверждение об ускорении произвольных программ Goat. Конкурирующая нагрузка на машину может влиять на результат.

The workload, correctness checks, timing boundaries, report format, and CI integration are described in [Performance Measurement](../../../docs/book/en/29-performance-measurement.md).

Нагрузка, проверки корректности, границы измерения, формат отчёта и включение в CI описаны в главе [«Измерение производительности»](../../../docs/book/ru/29-performance-measurement.md).

The [migration map](../native-performance/README.md) records original/C parity and the stable CTest scenarios.

[Карта миграции](../native-performance/README.md) содержит подтверждение сверки Python/C и стабильные сценарии CTest.
