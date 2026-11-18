#pragma once

#include "engine/AudioEngine.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace af::app
{

/// State of the audio output, for the UI and the log.
struct DeviceStatus
{
    bool open = false;
    std::string typeName; ///< "Windows Audio", "Windows Audio (Exclusive Mode)", "ALSA", ...
    std::string deviceName;
    std::string preferredName; ///< The device the user chose (saved in settings).
    double sampleRate = 0.0;
    int bufferSize = 0;
    int outputLatency = 0; ///< Samples, as reported by the driver.
    int outputChannels = 0;
    int xruns = -1;             ///< -1 when the driver does not report them.
    bool usingFallback = false; ///< The chosen device is missing; another one is playing.
    std::string error;          ///< Last error from opening a device; empty when fine.
};

/// What changed, reported on the message thread.
struct DeviceEvent
{
    enum class Kind
    {
        formatChanged, ///< A device started with a new sample rate or block size: recompile graphs.
        deviceLost,    ///< The chosen device disappeared; playback stopped. A fallback may be open.
        deviceRestored, ///< The chosen device is back and open again.
        noDevice,       ///< No output device could be opened.
    };

    Kind kind = Kind::formatChanged;
    DeviceStatus status;
};

/// Owns the audio device and connects it to the AudioEngine (architecture v0.2, sections 4.1
/// and 4.2, "Смена устройства"). Message thread only, except the audio callback.
///
/// - Windows uses WASAPI (shared, exclusive, low-latency shared); ASIO is off until decision L2.
/// - The user's choice is saved to `settingsFile` whenever it changes. A fallback device that
///   JUCE opens after the chosen one disappears is never saved over the choice.
/// - When the device stops (any change of device, rate or buffer, or removal), every graph is
///   taken back from the engine; the owner recompiles on formatChanged.
/// - When the chosen device disappears, playback stops; when it comes back, it is reopened.
class DeviceService final : private juce::ChangeListener,
                            private juce::Timer,
                            private juce::AudioIODeviceCallback
{
public:
    struct Options
    {
        std::filesystem::path settingsFile;
        /// Device types to use instead of the platform ones (tests use fake devices).
        std::vector<std::unique_ptr<juce::AudioIODeviceType>> deviceTypes;
        /// How often the service collects retired graphs and checks devices, in Hz.
        int timerHz = 30;
    };

    class Listener
    {
    public:
        virtual ~Listener() = default;
        /// Called on the message thread.
        virtual void deviceEvent(const DeviceEvent& event) = 0;

    protected:
        Listener() = default;
        Listener(const Listener&) = default;
        Listener& operator=(const Listener&) = default;
        Listener(Listener&&) = default;
        Listener& operator=(Listener&&) = default;
    };

    explicit DeviceService(Options options);
    ~DeviceService() override;

    DeviceService(const DeviceService&) = delete;
    DeviceService& operator=(const DeviceService&) = delete;
    DeviceService(DeviceService&&) = delete;
    DeviceService& operator=(DeviceService&&) = delete;

    /// Opens the saved device, or the default one. Never fails hard: without a device the app
    /// runs silently and the status says why.
    void initialise();

    void addListener(Listener* listener);
    void removeListener(Listener* listener);

    [[nodiscard]] engine::AudioEngine& engine() noexcept { return engine_; }
    [[nodiscard]] juce::AudioDeviceManager& deviceManager() noexcept { return manager_; }
    [[nodiscard]] DeviceStatus status() const;

    /// Graph format matching the open device (sample rate, block size); nullopt without a device.
    [[nodiscard]] std::optional<engine::RenderSpec> renderSpec() const;

    /// Reacts to device changes: saves the user's choice, detects loss and return of the chosen
    /// device, reports format changes. Runs on every change message and timer tick; public so
    /// tests can call it without a message loop.
    void refresh();

private:
    // juce::ChangeListener
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    // juce::Timer
    void timerCallback() override;
    // juce::AudioIODeviceCallback (audio thread except where noted)
    void
    audioDeviceIOCallbackWithContext(const float* const* inputs, int numInputs,
                                     float* const* outputs, int numOutputs, int numSamples,
                                     const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override; // callback not running
    void audioDeviceStopped() override;                                 // callback not running
    void audioDeviceError(const juce::String& message) override;

    void checkDevices();
    void saveSettingsIfChanged();
    [[nodiscard]] bool isDeviceAvailable(const juce::String& type, const juce::String& name);
    [[nodiscard]] bool anyDeviceAvailable();
    void emit(DeviceEvent::Kind kind);

    Options options_;
    engine::AudioEngine engine_;
    juce::AudioDeviceManager manager_;
    juce::ListenerList<Listener> listeners_;

    juce::String savedXml_; // what is on disk
    juce::String lastError_;
    juce::String lastDeviceName_; // device seen at the previous refresh
    double lastRate_ = 0.0;
    int lastBlock_ = -1; // forces an event on the first refresh
    bool lost_ = false;
    int ticks_ = 0;
    int restoreCooldown_ = 0;
    std::atomic<bool> errorPending_{false};
};

} // namespace af::app
