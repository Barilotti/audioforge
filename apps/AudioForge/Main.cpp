#include "core/Version.h"
#include "ui/shell/MainWindow.h"
#include "ui/theme/LookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>

namespace
{

class AudioForgeApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "AudioForge"; }
    const juce::String getApplicationVersion() override
    {
        return juce::String{std::string{af::core::versionString()}};
    }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String& commandLine) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel_);
        mainWindow_ = std::make_unique<af::ui::MainWindow>(getApplicationName(), commands_);

        // CI start-up/shut-down check: open the main window, then quit through the normal path.
        // Leaks or crashes on shutdown fail the test (JUCE's leak detector asserts in debug).
        if (commandLine.contains("--smoke-test"))
        {
            juce::Timer::callAfterDelay(1000, [this] { systemRequestedQuit(); });
        }
    }

    void shutdown() override
    {
        // Reverse order of creation (architecture section 4.2, application shutdown).
        mainWindow_.reset();
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    af::ui::LookAndFeel lookAndFeel_;
    juce::ApplicationCommandManager commands_;
    std::unique_ptr<af::ui::MainWindow> mainWindow_;
};

} // namespace

START_JUCE_APPLICATION(AudioForgeApplication)
