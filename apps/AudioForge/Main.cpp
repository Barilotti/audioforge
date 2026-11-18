#include "app/AppPaths.h"
#include "app/CrashReporting.h"
#include "app/DeviceService.h"
#include "app/Playback.h"
#include "assets/AudioBufferCache.h"
#include "core/Log.h"
#include "core/Version.h"
#include "ui/shell/MainWindow.h"
#include "ui/theme/LookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <string>

namespace
{

namespace aflog = af::core::log;

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
        const auto paths = af::app::AppPaths::forCurrentUser();
        af::app::installCrashHandler(paths.crashDumps);
        startLogging(paths);

        if (commandLine.contains("--crash-test"))
        {
            aflog::warning("Crash test requested from the command line");
            af::app::triggerTestCrash();
        }

        af::app::DeviceService::Options deviceOptions;
        deviceOptions.settingsFile = paths.settings / "AudioDevice.xml";
        devices_ = std::make_unique<af::app::DeviceService>(std::move(deviceOptions));
        devices_->initialise();

        af::assets::AudioBufferCache::Options cacheOptions;
        cacheOptions.budget = af::assets::MemoryBudget::defaultsFor(
            af::assets::ISystemMemory::host()->physicalBytes());
        cacheOptions.decoders = af::assets::DecoderRegistry::withBuiltInDecoders();
        cacheOptions.peakCache = std::make_shared<af::assets::PeakCache>(paths.cache / "peaks");
        cache_ = std::make_unique<af::assets::AudioBufferCache>(std::move(cacheOptions));
        playback_ = std::make_unique<af::app::Playback>(*devices_, *cache_);

        juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel_);
        mainWindow_ = std::make_unique<af::ui::MainWindow>(getApplicationName(), commands_, paths,
                                                           *devices_, *playback_);
        aflog::info("Main window opened");

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
        playback_.reset();
        devices_.reset(); // closes the device: the audio callback stops, graphs are reclaimed
        cache_.reset();   // after the device: retired graphs no longer hold its buffers
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        aflog::info("AudioForge shut down normally");
        aflog::shutdown();
    }

    void systemRequestedQuit() override { quit(); }

private:
    static void startLogging(const af::app::AppPaths& paths)
    {
        const auto logFile = aflog::initialise({.directory = paths.logs,
#ifdef NDEBUG
                                                .minLevel = aflog::Level::info,
                                                .alsoToStderr = false
#else
                                                .minLevel = aflog::Level::debug,
                                                .alsoToStderr = true
#endif
        });
        if (!logFile)
        {
            // Not fatal: messages go to stderr instead.
            aflog::error("Cannot start file logging: {}", logFile.error().describe());
        }

        aflog::info("AudioForge {} starting", af::core::buildDescription());
        aflog::info("{}", af::core::juceVersionString());
        aflog::info("OS: {}, CPU: {} x{}, RAM: {} MB",
                    juce::SystemStats::getOperatingSystemName().toStdString(),
                    juce::SystemStats::getCpuModel().toStdString(), juce::SystemStats::getNumCpus(),
                    juce::SystemStats::getMemorySizeInMegabytes());
        aflog::info("Data folder: {}", paths.localData.string());
    }

    af::ui::LookAndFeel lookAndFeel_;
    juce::ApplicationCommandManager commands_;
    std::unique_ptr<af::app::DeviceService> devices_;
    std::unique_ptr<af::assets::AudioBufferCache> cache_;
    std::unique_ptr<af::app::Playback> playback_;
    std::unique_ptr<af::ui::MainWindow> mainWindow_;
};

} // namespace

START_JUCE_APPLICATION(AudioForgeApplication)
