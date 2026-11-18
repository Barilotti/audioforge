#include "ui/shell/MainWindow.h"

#include "ui/settings/AudioSettingsComponent.h"
#include "ui/shell/AboutComponent.h"
#include "ui/shell/Commands.h"
#include "ui/theme/Theme.h"
#include "ui/timeline/TimelineView.h"
#include "ui/transport/TransportBar.h"

#include <filesystem>
#include <string>
#include <vector>

namespace af::ui
{

namespace
{
enum MenuIndex
{
    fileMenu = 0,
    transportMenu = 1,
    optionsMenu = 2,
    helpMenu = 3,
};

std::filesystem::path toPath(const juce::File& file)
{
    const auto utf8 = file.getFullPathName().toStdString(); // JUCE converts to UTF-8
    return std::filesystem::path{std::u8string{utf8.begin(), utf8.end()}};
}
} // namespace

MainWindow::MainWindow(const juce::String& name, juce::ApplicationCommandManager& commands,
                       app::AppPaths paths, app::DeviceService& devices, app::Playback& playback)
    : DocumentWindow(name, Theme::current().colours.windowBackground, DocumentWindow::allButtons)
    , commands_(commands)
    , paths_(std::move(paths))
    , devices_(devices)
    , playback_(playback)
{
    setUsingNativeTitleBar(true);

    auto shell = std::make_unique<ShellComponent>();
    shell_ = shell.get();
    shell_->centrePanel().setContent(std::make_unique<TimelineView>(devices_, playback_));
    shell_->bottomPanel().setContent(
        std::make_unique<TransportBar>(devices_, playback_, [this] { chooseAudioFile(); }));
    shell_->onAudioFilesDropped = [this](const juce::Array<juce::File>& files)
    { importFiles(files); };
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
    commands.add(CommandIds::newProject);
    commands.add(CommandIds::importAudio);
    commands.add(CommandIds::transportPlayStop);
    commands.add(CommandIds::transportPlayPause);
    commands.add(CommandIds::transportStop);
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
    else if (commandId == CommandIds::newProject)
    {
        result.setInfo(TRANS("New"), TRANS("Removes all layers"), TRANS("File"), 0);
        result.addDefaultKeypress('n', juce::ModifierKeys::commandModifier);
    }
    else if (commandId == CommandIds::importAudio)
    {
        result.setInfo(TRANS("Import Audio..."), TRANS("Adds audio files, each on a new layer"),
                       TRANS("File"), 0);
        result.addDefaultKeypress('i', juce::ModifierKeys::commandModifier);
    }
    else if (commandId == CommandIds::transportPlayStop)
    {
        result.setInfo(TRANS("Play / Stop"), TRANS("Starts playback, or stops and returns"),
                       TRANS("Transport"), 0);
        result.addDefaultKeypress(juce::KeyPress::spaceKey, juce::ModifierKeys::noModifiers);
    }
    else if (commandId == CommandIds::transportPlayPause)
    {
        result.setInfo(TRANS("Play / Pause"), TRANS("Starts or pauses playback"),
                       TRANS("Transport"), 0);
        result.addDefaultKeypress(juce::KeyPress::spaceKey, juce::ModifierKeys::shiftModifier);
    }
    else if (commandId == CommandIds::transportStop)
    {
        result.setInfo(TRANS("Stop"), TRANS("Stops playback and returns to the start position"),
                       TRANS("Transport"), 0);
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
        AudioSettingsComponent::show(devices_, playback_, this);
        return true;
    }
    if (info.commandID == CommandIds::newProject)
    {
        playback_.clear();
        return true;
    }
    if (info.commandID == CommandIds::importAudio)
    {
        chooseAudioFile();
        return true;
    }
    if (info.commandID == CommandIds::transportPlayStop)
    {
        playback_.transport().togglePlayStop();
        return true;
    }
    if (info.commandID == CommandIds::transportPlayPause)
    {
        playback_.transport().togglePlayPause();
        return true;
    }
    if (info.commandID == CommandIds::transportStop)
    {
        playback_.transport().stop();
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
    return {TRANS("File"), TRANS("Transport"), TRANS("Options"), TRANS("Help")};
}

juce::PopupMenu MainWindow::getMenuForIndex(int topLevelMenuIndex, const juce::String& /*menuName*/)
{
    juce::PopupMenu menu;
    if (topLevelMenuIndex == MenuIndex::fileMenu)
    {
        menu.addCommandItem(&commands_, CommandIds::newProject);
        menu.addCommandItem(&commands_, CommandIds::importAudio);
        menu.addSeparator();
        menu.addCommandItem(&commands_, juce::StandardApplicationCommandIDs::quit, TRANS("Exit"));
    }
    else if (topLevelMenuIndex == MenuIndex::transportMenu)
    {
        menu.addCommandItem(&commands_, CommandIds::transportPlayStop);
        menu.addCommandItem(&commands_, CommandIds::transportPlayPause);
        menu.addCommandItem(&commands_, CommandIds::transportStop);
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

void MainWindow::importFiles(const juce::Array<juce::File>& files)
{
    std::vector<std::filesystem::path> paths;
    for (const auto& file : files)
    {
        paths.push_back(toPath(file));
    }
    playback_.importFiles(paths);
}

void MainWindow::chooseAudioFile()
{
    chooser_ = std::make_unique<juce::FileChooser>(TRANS("Import Audio"), juce::File{},
                                                   ShellComponent::audioFilePatterns(), true);
    chooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::canSelectMultipleItems,
        [this](const juce::FileChooser& chooser) { importFiles(chooser.getResults()); });
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
