# Документация AudioForge

Документация ведётся на русском языке (ADR-0007).

- [Архитектура и план Phase 1 (v0.2, согласована 18.11.2026)](architecture/architecture-v0.2.md)
- [Architecture Decision Records](adr/) — решения, их контекст и альтернативы. Новое решение оформляется по шаблону [`0000-template.md`](adr/0000-template.md) в том же PR, что и код.
- [Решения перед публичным релизом](release/legal-checklist.md) — лицензии JUCE и ASIO SDK, подпись кода и другие юридические вопросы.

| ADR | Тема |
|---|---|
| [0001](adr/0001-stack-cpp-juce-cmake.md) | Стек C++20 + JUCE 8 + CMake, интерфейс на JUCE |
| [0002](adr/0002-domain-model-immutable-commands.md) | Доменная модель: Layer/Clip, неизменяемый документ, Commands и Undo |
| [0003](adr/0003-render-graph-threading.md) | RenderGraph и обмен данными между потоками |
| [0004](adr/0004-signal-flow-fx-chains.md) | Порядок прохождения сигнала и цепочки эффектов |
| [0005](adr/0005-preview-render-quality.md) | Preview Quality и Render Quality |
| [0006](adr/0006-asset-memory-budgets.md) | Ассеты в памяти и бюджеты памяти |
| [0007](adr/0007-languages-localization.md) | Языки проекта и локализация |
| [0008](adr/0008-project-format-timeticks.md) | Формат проекта и хранение времени |
| [0009](adr/0009-development-process.md) | Репозиторий, среда разработки и процесс |
| [0010](adr/0010-build-structure.md) | Структура сборки и зависимостей |
| [0011](adr/0011-model-invariants.md) | Минимальная доменная модель и её инварианты |
| [0012](adr/0012-wav-decoding.md) | Декодирование WAV и AudioBufferCache |
| [0013](adr/0013-engine-graph.md) | Граф рендеринга, голос клипа и передача графа |
