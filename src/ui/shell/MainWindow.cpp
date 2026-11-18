#include "ui/shell/MainWindow.h"

#include "ui/settings/AudioSettingsComponent.h"
#include "ui/shell/AboutComponent.h"
#include "ui/shell/Commands.h"
#include "ui/theme/Theme.h"

namespace af::ui
{

namespace
{
enum MenuIndex
{
    fileMenu = 0,
    optionsMenu = 1,
    helpMenu = 2,
};
} // namespace

MainWindow::MainWindow(const juce::String& name, juce::ApplicationCommandManager& commands,
                       app::AppPaths paths, app::DeviceService& devices)
    : DocumentWindow(name, Theme::current().colours.windowBackground, DocumentWindow::allButtons)
    , commands_(commands)
    , paths_(std::move(paths))
    , devices_(devices)
{
    setUsingNativeTitleBar(true);

    auto shell = std::make_unique<ShellComponent>();
    shell_ = shell.get();
    setContentOwned(shell.release(), false);

    commands_.registerAllCommandsForTarget(this);
    // The application object provides StandardApplicationCommandIDs::quit.
    commands_.registerAllCommandsForTarget(juce::JUCEApplication::getInstance());
    commands_.setFirstCommandTarget(this);
    addKeyListener(commands_.getKeyMappings());

    setMenuBar(this, Theme::current().metrics.menuBarHeight);
    setApplicationCommandManagerToWatch(&commands_);

    setResizable(true, false);
    setResizeLimits(960, 600, 16384, 16384);

    // Open at a comfortable size that fits the current display.
    const auto display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto area = display != nullptr ? display->userBounds.toNearestInt()
                                         : juce::Rectangle<int>{0, 0, 1440, 900};
    centreWithSize(juce::jmin(1440, area.getWidth() - 40), juce::jmin(900, area.getHeight() - 40));
    setVisible(true);
    devices_.addListener(this);
}

MainWindow::~MainWindow()
{
    devices_.removeListener(this);
    setMenuBar(nullptr);
    commands_.setFirstCommandTarget(nullptr);
}

void MainWindow::closeButtonPressed()
{
    if (auto* app = juce::JUCEApplicationBase::getInstance())
    {
        app->systemRequestedQuit();
    }
}

juce::ApplicationCommandTarget* MainWindow::getNextCommandTarget()
{
    // The application object handles StandardApplicationCommandIDs::quit.
    return juce::JUCEApplication::getInstance();
}

void MainWindow::getAllCommands(juce::Array<juce::CommandID>& commands)
{
    commands.add(CommandIds::showAbout);
    commands.add(CommandIds::openLogsFolder);
    commands.add(CommandIds::showAudioSettings);
}

void MainWindow::getCommandInfo(juce::CommandID commandId, juce::ApplicationCommandInfo& result)
{
    if (commandId == CommandIds::showAbout)
    {
        result.setInfo(TRANS("About AudioForge..."), TRANS("Shows version information"),
                       TRANS("Help"), 0);
    }
    else if (commandId == CommandIds::showAudioSettings)
    {
        result.setInfo(TRANS("Audio Settings..."),
                       TRANS("Chooses the audio output device, sample rate and buffer size"),
                       TRANS("Options"), 0);
    }
    else if (commandId == CommandIds::openLogsFolder)
    {
        result.setInfo(TRANS("Open Logs Folder"),
                       TRANS("Opens the folder with log files and crash reports"), TRANS("Help"),
                       0);
    }
}

bool MainWindow::perform(const InvocationInfo& info)
{
    if (info.commandID == CommandIds::showAbout)
    {
        AboutComponent::show(this);
        return true;
    }
    if (info.commandID == CommandIds::showAudioSettings)
    {
        AudioSettingsComponent::show(devices_, this);
        return true;
    }
    if (info.commandID == CommandIds::openLogsFolder)
    {
        const juce::File folder{juce::String{paths_.localData.wstring().c_str()}};
        folder.createDirectory();
        folder.startAsProcess();
        return true;
    }
    return false;
}

juce::StringArray MainWindow::getMenuBarNames()
{
    return {TRANS("File"), TRANS("Options"), TRANS("Help")};
}

juce::PopupMenu MainWindow::getMenuForIndex(int topLevelMenuIndex, const juce::String& /*menuName*/)
{
    juce::PopupMenu menu;
    if (topLevelMenuIndex == MenuIndex::fileMenu)
    {
        menu.addCommandItem(&commands_, juce::StandardApplicationCommandIDs::quit, TRANS("Exit"));
    }
    else if (topLevelMenuIndex == MenuIndex::optionsMenu)
    {
        menu.addCommandItem(&commands_, CommandIds::showAudioSettings);
    }
    else if (topLevelMenuIndex == MenuIndex::helpMenu)
    {
        menu.addCommandItem(&commands_, CommandIds::openLogsFolder);
        menu.addSeparator();
        menu.addCommandItem(&commands_, CommandIds::showAbout);
    }
    return menu;
}

void MainWindow::menuItemSelected(int /*menuItemId*/, int /*topLevelMenuIndex*/)
{
    // All menu items are commands; the command manager invokes them.
}

void MainWindow::deviceEvent(const app::DeviceEvent& event)
{
    if (event.kind != app::DeviceEvent::Kind::deviceLost)
    {
        return;
    }
    auto text = TRANS("The audio device \"%1\" was disconnected. Playback has stopped.")
                    .replace("%1", juce::String::fromUTF8(event.status.preferredName.c_str()));
    if (event.status.open)
    {
        text << "\n"
             << TRANS("Now using:") << " "
             << juce::String::fromUTF8(event.status.deviceName.c_str());
    }
    text << "\n" << TRANS("AudioForge switches back when the device is connected again.");
    juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                     .withIconType(juce::MessageBoxIconType::WarningIcon)
                                     .withTitle(TRANS("Audio device disconnected"))
                                     .withMessage(text)
                                     .withButton(TRANS("OK"))
                                     .withAssociatedComponent(this),
                                 nullptr);
}

} // namespace af::ui
