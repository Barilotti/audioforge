# Modules in dependency order. The allowed dependencies follow the matrix in
# docs/architecture (section 2). Do not add a dependency that goes "up" the list.

# Core: ids, time, Result/Error, logging, real-time primitives. No public JUCE types.
af_add_module(core
    SOURCES
        core/Log.cpp
        core/PluralRules.cpp
        core/RealtimeThread.cpp
        core/Result.cpp
        core/TimeTicks.cpp
        core/Uuid.cpp
        core/Version.cpp
        "${CMAKE_CURRENT_BINARY_DIR}/generated/core/BuildInfo.cpp"
    PRIVATE_DEPS AudioForge::juce)
configure_file(core/BuildInfo.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/core/BuildInfo.cpp" @ONLY)

# Domain model, commands, undo. Depends only on core: no JUCE, no audio, no UI.
af_add_module(model PUBLIC_DEPS AudioForge::core)

# Project file format (JSON) and migrations.
af_add_module(serialization PUBLIC_DEPS AudioForge::model AudioForge::core)

# DSP building blocks. Real-time safe, no model, no UI.
af_add_module(dsp PUBLIC_DEPS AudioForge::core)

# Asset decoding, buffer cache, peaks.
af_add_module(assets PUBLIC_DEPS AudioForge::core)

# Render graph, real-time player, offline renderer, devices.
af_add_module(engine PUBLIC_DEPS AudioForge::model AudioForge::dsp AudioForge::assets)

# Audio analysis (phase 2).
af_add_module(analysis PUBLIC_DEPS AudioForge::dsp AudioForge::assets)

# Sample library providers (file system in phase 1).
af_add_module(library PUBLIC_DEPS AudioForge::assets AudioForge::core)

# Application services: ProjectSession, autosave, export, settings.
af_add_module(app
    SOURCES
        app/AppPaths.cpp
        app/CrashReporting.cpp
    PUBLIC_DEPS
        AudioForge::model
        AudioForge::serialization
        AudioForge::engine
        AudioForge::library
        AudioForge::analysis
    PRIVATE_DEPS
        AudioForge::juce)
if(WIN32)
    target_link_libraries(af_app PRIVATE Dbghelp)
endif()

# User interface. The only module allowed to use JUCE GUI classes.
af_add_module(ui
    SOURCES
        ui/shell/AboutComponent.cpp
        ui/shell/MainWindow.cpp
        ui/shell/Panel.cpp
        ui/shell/ShellComponent.cpp
        ui/theme/LookAndFeel.cpp
        ui/theme/Theme.cpp
    PUBLIC_DEPS
        AudioForge::app
        AudioForge::juce)
