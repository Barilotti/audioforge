#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace af::ui
{

/// Application command ids. Values stay stable: they will be stored in key-mapping settings.
/// StandardApplicationCommandIDs (quit, undo, redo, cut, copy, paste...) are used where they fit.
enum CommandIds : juce::CommandID
{
    showAbout = 0x2001,
    openLogsFolder = 0x2002,
    showAudioSettings = 0x2003,
    importAudio = 0x2004,
    newProject = 0x2005,

    transportPlayStop = 0x2101,
    transportPlayPause = 0x2102,
    transportStop = 0x2103,
};

} // namespace af::ui
