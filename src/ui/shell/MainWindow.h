#pragma once

#include "app/AppPaths.h"
#include "app/DeviceService.h"
#include "app/Playback.h"
#include "ui/shell/ShellComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace af::ui
{

/// The application's main window: menu bar, command handling and the panel shell.
class MainWindow final : public juce::DocumentWindow,
                         public juce::ApplicationCommandTarget,
                         private juce::MenuBarModel,
                         private app::DeviceService::Listener
{
public:
    MainWindow(const juce::String& name, juce::ApplicationCommandManager& commands,
               app::AppPaths paths, app::DeviceService& devices, app::Playback& playback);
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

    // DeviceService::Listener
    void deviceEvent(const app::DeviceEvent& event) override;

    void chooseAudioFile();

    juce::ApplicationCommandManager& commands_;
    app::AppPaths paths_;
    app::DeviceService& devices_;
    app::Playback& playback_;
    std::unique_ptr<juce::FileChooser> chooser_;
    ShellComponent* shell_ = nullptr; // owned by the window as its content component

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace af::ui
