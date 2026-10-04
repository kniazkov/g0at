# Goat изнутри. Устройство интерпретатора, статического анализатора и нативного бэкенда

Красные блоки отмечают ограничения и известные дефекты описываемой реализации.

## Оглавление

### Часть I. Назначение и общая структура

1. [Назначение проекта](01-project-purpose.md)
2. [Язык, который исполняет интерпретатор](02-implemented-language.md)
3. [Путь программы через систему](03-program-pipeline.md)

### Часть II. Исходный текст и представление программы

4. [Организация реализации на C](04-implementation-in-c.md)
5. [Лексический анализ](05-lexical-analysis.md)
6. [Синтаксический анализ](06-syntax-analysis.md)
7. [AST, области видимости и связывание имён](07-ast-and-name-binding.md)

### Часть III. Байткод и исполнительная среда

8. [Генерация байткода](08-bytecode-generation.md)
9. [Виртуальная машина и модель значений](09-vm-and-values.md)
10. [Контексты, функции и замыкания](10-contexts-and-closures.md)
11. [Управление памятью](11-memory-management.md)
12. [Исключения и ошибки выполнения](12-exceptions.md)
13. [Встроенные функции](13-builtins.md)

### Часть IV. Статический анализ

14. [Абстрактные значения и решётка](14-abstract-values.md)
15. [Абстрактное состояние](15-abstract-state.md)
16. [Анализ выражений и управления](16-expression-and-control-analysis.md)
17. [Анализ вызовов и специализации](17-call-analysis-and-specializations.md)
18. [Граф вызовов и рекурсия](18-call-graph-and-recursion.md)
19. [Побочные эффекты и чистота](19-effects-and-purity.md)
20. [Допуск к нативной компиляции](20-native-eligibility.md)
21. [Преобразования AST и сохранение семантики](21-ast-transformations.md)

### Часть V. Нативный бэкенд

22. [Генерация C](22-c-code-generation.md)
23. [Нативный ABI](23-native-abi.md)
24. [Компиляция и загрузка библиотек](24-library-compilation-and-loading.md)
25. [Выбор нативного выполнения и возврат к VM](25-native-dispatch-and-vm-fallback.md)
26. [Раздельная компиляция](26-separate-compilation.md)

### Часть VI. Проверка и исследование реализации

27. [Наблюдение за работой системы](27-observability.md)
28. [Тестирование](28-testing.md)
29. [Измерение производительности](29-performance-measurement.md)

### Приложения

- [А. Термины](appendix-a-terminology.md)
- [Б. Язык](appendix-b-language.md)
- [В. Инструкции VM](appendix-c-vm-instructions.md)
- [Г. Встроенные функции](appendix-d-builtins.md)
- [Д. Интерфейсы запуска](appendix-e-launch-interfaces.md)
- [Е. Карта реализации](appendix-f-implementation-map.md)
- [Ж. Границы реализации](appendix-g-implementation-boundaries.md)
- [З. История решений](appendix-h-decision-history.md)
