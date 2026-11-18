#include "app/DeviceService.h"

#include "core/Log.h"

#include <utility>

namespace af::app
{

namespace
{

namespace aflog = core::log;

constexpr int checkEveryTicks = 15;    // device checks at about 2 Hz; graph collection every tick
constexpr int restoreRetryChecks = 20; // about 10 s between failed attempts to reopen a device

juce::File toFile(const std::filesystem::path& path)
{
    return juce::File{juce::String{path.wstring().c_str()}};
}

std::string str(const juce::String& s)
{
    return s.toStdString();
}

} // namespace

DeviceService::DeviceService(Options options)
    : options_(std::move(options))
{
    for (auto& type : options_.deviceTypes)
    {
        manager_.addAudioDeviceType(std::move(type));
    }
    options_.deviceTypes.clear();
    manager_.addAudioCallback(this);
}

DeviceService::~DeviceService()
{
    stopTimer();
    manager_.removeChangeListener(this);
    manager_.removeAudioCallback(this); // calls audioDeviceStopped() if a device is running
    manager_.closeAudioDevice();
    engine_.deviceStopped();
}

void DeviceService::initialise()
{
    std::unique_ptr<juce::XmlElement> saved;
    const auto file = toFile(options_.settingsFile);
    if (file.existsAsFile())
    {
        savedXml_ = file.loadFileAsString();
        saved = juce::XmlDocument::parse(savedXml_);
        if (saved == nullptr || !saved->hasTagName("DEVICESETUP"))
        {
            aflog::warning("Audio device settings are unreadable and will be replaced: {}",
                           options_.settingsFile.string());
            saved.reset();
            savedXml_.clear();
        }
    }

    lastError_ = manager_.initialise(0, 2, saved.get(), true);
    if (lastError_.isNotEmpty())
    {
        aflog::warning("Audio device: {}", str(lastError_));
    }

    manager_.addChangeListener(this);
    if (options_.timerHz > 0)
    {
        startTimerHz(options_.timerHz);
    }
    refresh();
}

void DeviceService::addListener(Listener* listener)
{
    listeners_.add(listener);
}

void DeviceService::removeListener(Listener* listener)
{
    listeners_.remove(listener);
}

DeviceStatus DeviceService::status() const
{
    DeviceStatus s;
    auto* device = manager_.getCurrentAudioDevice();
    if (const auto xml = manager_.createStateXml())
    {
        s.preferredName = str(xml->getStringAttribute("audioOutputDeviceName"));
    }
    s.error = str(lastError_);
    s.usingFallback = lost_ && device != nullptr;
    if (device == nullptr)
    {
        return s;
    }
    s.open = true;
    s.typeName = str(device->getTypeName());
    s.deviceName = str(device->getName());
    s.sampleRate = device->getCurrentSampleRate();
    s.bufferSize = device->getCurrentBufferSizeSamples();
    s.outputLatency = device->getOutputLatencyInSamples();
    s.outputChannels = device->getActiveOutputChannels().countNumberOfSetBits();
    s.xruns = device->getXRunCount();
    return s;
}

std::optional<engine::RenderSpec> DeviceService::renderSpec() const
{
    auto* device = manager_.getCurrentAudioDevice();
    if (device == nullptr)
    {
        return std::nullopt;
    }
    return engine::RenderSpec{static_cast<core::SampleRate>(device->getCurrentSampleRate()),
                              device->getCurrentBufferSizeSamples(),
                              engine::RenderQuality::asExport};
}

bool DeviceService::isDeviceAvailable(const juce::String& type, const juce::String& name)
{
    for (auto* deviceType : manager_.getAvailableDeviceTypes())
    {
        if (deviceType->getTypeName() == type)
        {
            return deviceType->getDeviceNames(false).contains(name);
        }
    }
    return false;
}

bool DeviceService::anyDeviceAvailable()
{
    for (auto* deviceType : manager_.getAvailableDeviceTypes())
    {
        if (!deviceType->getDeviceNames(false).isEmpty())
        {
            return true;
        }
    }
    return false;
}

void DeviceService::refresh()
{
    saveSettingsIfChanged();
    checkDevices();
}

void DeviceService::checkDevices()
{
    if (errorPending_.exchange(false))
    {
        if (auto* device = manager_.getCurrentAudioDevice())
        {
            lastError_ = device->getLastError();
            aflog::warning("Audio device error: {}", str(lastError_));
        }
    }

    const auto preferred = manager_.createStateXml();
    const auto preferredType =
        preferred ? preferred->getStringAttribute("deviceType") : juce::String{};
    const auto preferredName =
        preferred ? preferred->getStringAttribute("audioOutputDeviceName") : juce::String{};
    auto* device = manager_.getCurrentAudioDevice();

    const bool onPreferred =
        device != nullptr && (preferredName.isEmpty() || (device->getName() == preferredName &&
                                                          device->getTypeName() == preferredType));

    // The chosen device is back: reopen it (this also stops a fallback device cleanly).
    if (restoreCooldown_ > 0)
    {
        --restoreCooldown_;
    }
    else if (!onPreferred && preferredName.isNotEmpty() &&
             isDeviceAvailable(preferredType, preferredName))
    {
        aflog::info("Audio device '{}' is available again; reopening it", str(preferredName));
        lastError_ = manager_.initialise(0, 2, preferred.get(), true);
        device = manager_.getCurrentAudioDevice();
        if (device == nullptr || device->getName() != preferredName)
        {
            restoreCooldown_ = restoreRetryChecks; // listed but failed to open: do not thrash
        }
    }
    else if (device == nullptr && anyDeviceAvailable())
    {
        // Nothing is open (every device had gone) and something is connected again. JUCE only
        // reacts to list changes while a device is open, so reopen the choice or the default.
        lastError_ = manager_.initialise(0, 2, preferred.get(), true);
        device = manager_.getCurrentAudioDevice();
        if (device == nullptr)
        {
            restoreCooldown_ = restoreRetryChecks;
        }
    }

    const bool nowOnPreferred =
        device != nullptr && (preferredName.isEmpty() || (device->getName() == preferredName &&
                                                          device->getTypeName() == preferredType));
    if (!nowOnPreferred && !lost_)
    {
        lost_ = true;
        engine_.setPlaying(false);
        aflog::warning("Audio device '{}' is not available; {}",
                       preferredName.isNotEmpty() ? str(preferredName) : std::string{"(default)"},
                       device != nullptr ? "using '" + str(device->getName()) + "' instead"
                                         : "no output device");
        emit(DeviceEvent::Kind::deviceLost);
    }
    else if (nowOnPreferred && lost_)
    {
        lost_ = false;
        aflog::info("Audio device '{}' restored", str(device->getName()));
        emit(DeviceEvent::Kind::deviceRestored);
    }

    const auto name = device != nullptr ? device->getName() : juce::String{};
    const double rate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
    const int block = device != nullptr ? device->getCurrentBufferSizeSamples() : 0;
    if (name != lastDeviceName_ || rate != lastRate_ || block != lastBlock_)
    {
        lastDeviceName_ = name;
        lastRate_ = rate;
        lastBlock_ = block;
        if (device != nullptr)
        {
            aflog::info("Audio output: {} / {}, {} Hz, {} samples", str(device->getTypeName()),
                        str(name), rate, block);
            emit(DeviceEvent::Kind::formatChanged);
        }
        else
        {
            emit(DeviceEvent::Kind::noDevice);
        }
    }
}

void DeviceService::saveSettingsIfChanged()
{
    const auto xml = manager_.createStateXml();
    if (xml == nullptr)
    {
        return;
    }
    const auto text = xml->toString();
    if (text == savedXml_)
    {
        return;
    }
    const auto file = toFile(options_.settingsFile);
    const auto temp = file.getSiblingFile(file.getFileName() + ".tmp");
    if (!file.getParentDirectory().createDirectory() || !temp.replaceWithText(text) ||
        !temp.moveFileTo(file))
    {
        aflog::error("Cannot save audio device settings to {}", options_.settingsFile.string());
        return;
    }
    savedXml_ = text;
}

void DeviceService::emit(DeviceEvent::Kind kind)
{
    const DeviceEvent event{kind, status()};
    listeners_.call([&event](Listener& l) { l.deviceEvent(event); });
}

void DeviceService::changeListenerCallback(juce::ChangeBroadcaster* /*source*/)
{
    refresh();
}

void DeviceService::timerCallback()
{
    engine_.collectRetired();
    if (++ticks_ >= checkEveryTicks)
    {
        ticks_ = 0;
        refresh();
    }
}

void DeviceService::audioDeviceIOCallbackWithContext(
    const float* const* /*inputs*/, int /*numInputs*/, float* const* outputs, int numOutputs,
    int numSamples, const juce::AudioIODeviceCallbackContext& /*context*/)
{
    engine_.process(outputs, numOutputs, numSamples);
}

void DeviceService::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    // The callback is not running here: take every graph back and prepare for the new format.
    engine_.deviceStopped();
    engine_.prepare(static_cast<core::SampleRate>(device->getCurrentSampleRate()),
                    device->getCurrentBufferSizeSamples());
}

void DeviceService::audioDeviceStopped()
{
    engine_.deviceStopped();
}

void DeviceService::audioDeviceError(const juce::String& /*message*/)
{
    errorPending_ = true; // may run on a driver thread: just flag it for the message thread
}

} // namespace af::app
