#pragma once

#include "app/AppPaths.h"
#include "ui/shell/ShellComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace af::ui
{

/// The application's main window: menu bar, command handling and the panel shell.
class MainWindow final : public juce::DocumentWindow,
                         public juce::ApplicationCommandTarget,
                         private juce::MenuBarModel
{
public:
    MainWindow(const juce::String& name, juce::ApplicationCommandManager& commands,
               app::AppPaths paths);
    ~MainWindow() override;

    [[nodiscard]] ShellComponent& shell() noexcept { return *shell_; }

    // DocumentWindow
    void closeButtonPressed() override;

    // ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override;
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID commandId, juce::ApplicationCommandInfo& result) override;
    bool perform(const InvocationInfo& info) override;

private:
    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemId, int topLevelMenuIndex) override;

    juce::ApplicationCommandManager& commands_;
    app::AppPaths paths_;
    ShellComponent* shell_ = nullptr; // owned by the window as its content component

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace af::ui
