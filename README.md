# AudioForge

Универсальная рабочая станция для звукового дизайна под Windows 10/11.

Проект находится на этапе **S0 «Окно»** (Phase 1). Архитектура и решения описаны в [`docs/`](docs/README.md).

## Стек

C++20, [JUCE 8](https://juce.com) (UI, аудио, форматы, DSP), CMake + [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake), Catch2 для тестов. Все версии зависимостей зафиксированы в `cmake/Dependencies.cmake`.

## Сборка на Windows

Нужны Visual Studio 2022 (компонент «Разработка классических приложений на C++»), CMake 3.25+ и Git.

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-msvc-release
ctest --preset windows-msvc-release
```

Приложение: `build\windows-msvc\apps\AudioForge\AudioForge_artefacts\RelWithDebInfo\AudioForge.exe`.

## Сборка на Linux (разработка и CI)

Нужны Clang (или GCC 13+), CMake 3.25+, Ninja и зависимости JUCE:

```bash
sudo apt install clang ninja-build libasound2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev \
  libxrender-dev libgl1-mesa-dev
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug
```

Чтобы не скачивать зависимости заново для каждой папки сборки, задайте `CPM_SOURCE_CACHE`, например `export CPM_SOURCE_CACHE=~/.cache/cpm`.

## Структура

```
apps/AudioForge   приложение
apps/af-render    рендер проекта из командной строки (без GUI)
src/<module>      модули: core, model, serialization, dsp, assets, engine, analysis, library, app, ui
tests/            юнит-тесты и проверка правил зависимостей между модулями
cmake/            CPM, предупреждения компилятора, санитайзеры, версия
```

Модули зависят друг от друга только «сверху вниз». Правило проверяется тестом `architecture.module_includes`: запрещённый `#include` (например, UI-код в `model`) ломает сборку тестов.

## Правила разработки

- Одна задача — один PR; CI и тесты обязательны.
- Код, комментарии, сообщения коммитов и интерфейс — на английском; документация — на русском.
- Форматирование — `clang-format` (конфигурация в `.clang-format`).
