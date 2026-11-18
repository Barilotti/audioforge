# Бенчмарки движка

Решение и пороги: ADR-0017.

## Запуск локально

```
cmake --preset linux-clang-release
cmake --build --preset linux-clang-release --target af_benchmarks
build/linux-clang-release/benchmarks/af_benchmarks --benchmark_repetitions=5 \
  --benchmark_report_aggregates_only=true --benchmark_out=bench.json --benchmark_out_format=json
python3 benchmarks/check_regression.py bench.json benchmarks/baseline/linux-ci.json
```

На Windows пресет `windows-msvc`, конфигурация RelWithDebInfo, файл `af_benchmarks.exe`. Отчёт печатает время, отношение к эталону, изменение относительно базовой линии и нагрузку движка (доля длительности блока).

## Если CI сообщил о регрессии

1. Открыть артефакт `benchmarks-linux` и вывод шага Benchmarks.
2. Если замедление случайное, исправить код.
3. Если замедление ожидаемо (новая функция стоит CPU), обновить базовую линию отдельным коммитом и объяснить причину в сообщении коммита:

```
python3 benchmarks/check_regression.py benchmarks.json benchmarks/baseline/linux-ci.json --write-baseline
```

Отчёт `benchmarks.json` для этого берётся из артефакта CI, а не с локальной машины.
