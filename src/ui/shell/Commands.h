#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace af::ui
{

/// Application command ids. Values stay stable: they will be stored in key-mapping settings.
/// StandardApplicationCommandIDs (quit, undo, redo, cut, copy, paste...) are used where they fit.
enum CommandIds : juce::CommandID
{
    showAbout = 0x2001,
};

} // namespace af::ui
