#include "ui/shell/MainWindow.h"

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
    helpMenu = 1,
};
} // namespace

MainWindow::MainWindow(const juce::String& name, juce::ApplicationCommandManager& commands)
    : DocumentWindow(name, Theme::current().colours.windowBackground, DocumentWindow::allButtons)
    , commands_(commands)
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
}

MainWindow::~MainWindow()
{
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
}

void MainWindow::getCommandInfo(juce::CommandID commandId, juce::ApplicationCommandInfo& result)
{
    if (commandId == CommandIds::showAbout)
    {
        result.setInfo(TRANS("About AudioForge..."), TRANS("Shows version information"),
                       TRANS("Help"), 0);
    }
}

bool MainWindow::perform(const InvocationInfo& info)
{
    if (info.commandID == CommandIds::showAbout)
    {
        AboutComponent::show(this);
        return true;
    }
    return false;
}

juce::StringArray MainWindow::getMenuBarNames()
{
    return {TRANS("File"), TRANS("Help")};
}

juce::PopupMenu MainWindow::getMenuForIndex(int topLevelMenuIndex, const juce::String& /*menuName*/)
{
    juce::PopupMenu menu;
    if (topLevelMenuIndex == MenuIndex::fileMenu)
    {
        menu.addCommandItem(&commands_, juce::StandardApplicationCommandIDs::quit, TRANS("Exit"));
    }
    else if (topLevelMenuIndex == MenuIndex::helpMenu)
    {
        menu.addCommandItem(&commands_, CommandIds::showAbout);
    }
    return menu;
}

void MainWindow::menuItemSelected(int /*menuItemId*/, int /*topLevelMenuIndex*/)
{
    // All menu items are commands; the command manager invokes them.
}

} // namespace af::ui
