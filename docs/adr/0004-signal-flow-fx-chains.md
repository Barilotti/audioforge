# ADR-0004: Порядок прохождения сигнала и цепочки эффектов (D9)

- **Статус:** принято
- **Дата:** 2026-11-18
- **Связано:** архитектура v0.2, раздел 4.4

## Решение
```
Clip:   Source → Resample → Fade In/Out → Clip Gain → [Clip FX] (с хвостом) → Σ слоя
Layer:  Σ клипов → [Layer FX] (pre-fader) → Gain → Pan → Mute/Solo → Meter → родитель
Group:  Σ детей → [Group FX] → Gain → Pan → Mute/Solo → Meter → родитель
Master: Σ → [Master FX] → Master Gain → Meter → Output
Только экспорт: Normalization → Dither → Encoder
```
- Единый интерфейс `IProcessor` на всех уровнях, `ProcessorRegistry`, версия алгоритма в проекте.
- Компенсация задержки (PDC) и хвосты эффектов поддерживаются с этапа S6.
- Внутренние шины стерео в Phase 1; тип `ChannelLayout` в API с первого дня.

## Последствия
- Fade до нелинейных эффектов исключает щелчки на срезах; Clip Gain работает как входной уровень эффектов.
- Нормализация и дизер не попадают в Preview по умолчанию.
