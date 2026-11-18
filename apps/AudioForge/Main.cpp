#include "core/Version.h"
#include "ui/MainComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace
{

class MainWindow final : public juce::DocumentWindow
{
public:
    explicit MainWindow(const juce::String& name)
        : DocumentWindow(name, juce::Colour{0xff1b1d21}, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(new af::ui::MainComponent(), true);
        setResizable(true, true);
        setResizeLimits(960, 600, 10000, 10000);
        centreWithSize(getWidth(), getHeight());
        setVisible(true);
    }

    ~MainWindow() override = default;

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

class AudioForgeApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "AudioForge"; }
    const juce::String getApplicationVersion() override
    {
        return juce::String{std::string{af::core::versionString()}};
    }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& /*commandLine*/) override
    {
        mainWindow = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override { mainWindow.reset(); }

    void systemRequestedQuit() override { quit(); }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

} // namespace

START_JUCE_APPLICATION(AudioForgeApplication)
