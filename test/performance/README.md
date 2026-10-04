# Native performance check / Проверка нативной производительности

The benchmark compares VM execution with native execution of a precompiled integer call tree. Build Goat in Release mode first. Run these commands from the repository root.

Бенчмарк сравнивает исполнение заранее скомпилированного дерева целочисленных вызовов в VM и нативном режиме. Сначала соберите Goat в Release. Выполняйте команды из корня репозитория.

## Linux

```bash
bash scripts/build_release.sh
python3 scripts/check_native_performance.py build/release/goat build/native-performance
```

## Windows (PowerShell)

```powershell
.\scripts\build_release_mingw.cmd
python .\scripts\check_native_performance.py .\build\release\goat.exe .\build\native-performance
```

Python uses only its standard library. `CC` selects one compiler executable for native preparation. The output directory retains generated source, compiled programs, the native library, dispatch reports, and `timings.json`.

Python использует только стандартную библиотеку. `CC` задаёт один исполняемый файл компилятора для нативной подготовки. В выходном каталоге сохраняются сгенерированный исходный текст, скомпилированные программы, нативная библиотека, отчёты о диспетчеризации и `timings.json`.

Both variants are compiled before timing. Each gets a warm-up and five measured process runs. The check verifies results and actual native entry, and passes only when the native median is strictly lower than the VM median. This measures the whole execution process; it is not a general speedup claim for Goat programs. Host contention can affect the result.

Оба варианта компилируются до измерения. Для каждого выполняются прогрев и пять измеряемых запусков процесса. Проверка сверяет результаты и фактический вход в нативный код и проходит только при медиане нативного времени строго ниже медианы VM. Измеряется весь процесс исполнения; это не утверждение об ускорении произвольных программ Goat. Конкурирующая нагрузка на машину может влиять на результат.

The workload, correctness checks, timing boundaries, report format, and CI integration are described in [Performance Measurement](../../docs/book/en/29-performance-measurement.md).

Нагрузка, проверки корректности, границы измерения, формат отчёта и включение в CI описаны в главе [«Измерение производительности»](../../docs/book/ru/29-performance-measurement.md).
