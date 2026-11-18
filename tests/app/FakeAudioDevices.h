#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <vector>

namespace af::test
{

/// Which fake devices are "plugged in". Tests add and remove names, then call
/// FakeDeviceType::devicesChanged() as a driver would on hot-plug.
struct FakeWorld
{
    juce::StringArray devices{"Fake A", "Fake B"};
};

class FakeDevice final : public juce::AudioIODevice
{
public:
    FakeDevice(const juce::String& deviceName, FakeWorld& world)
        : AudioIODevice(deviceName, "Fake")
        , world_(world)
    {
    }

    ~FakeDevice() override { close(); }

    FakeDevice(const FakeDevice&) = delete;
    FakeDevice& operator=(const FakeDevice&) = delete;
    FakeDevice(FakeDevice&&) = delete;
    FakeDevice& operator=(FakeDevice&&) = delete;

    juce::StringArray getOutputChannelNames() override { return {"Left", "Right"}; }
    juce::StringArray getInputChannelNames() override { return {}; }
    juce::Array<double> getAvailableSampleRates() override
    {
        return {44'100.0, 48'000.0, 96'000.0};
    }
    juce::Array<int> getAvailableBufferSizes() override { return {64, 128, 256, 512}; }
    int getDefaultBufferSize() override { return 256; }

    juce::String open(const juce::BigInteger& /*inputs*/, const juce::BigInteger& outputs,
                      double rate, int bufferSize) override
    {
        if (!world_.devices.contains(getName()))
        {
            return "Device is not connected";
        }
        outputs_ = outputs;
        rate_ = rate > 0.0 ? rate : 48'000.0;
        bufferSize_ = bufferSize > 0 ? bufferSize : 256;
        open_ = true;
        return {};
    }

    void close() override
    {
        stop();
        open_ = false;
    }

    bool isOpen() override { return open_; }

    void start(juce::AudioIODeviceCallback* callback) override
    {
        if (callback != nullptr && callback_ == nullptr)
        {
            callback->audioDeviceAboutToStart(this);
            callback_ = callback;
        }
    }

    void stop() override
    {
        if (auto* callback = std::exchange(callback_, nullptr))
        {
            callback->audioDeviceStopped();
        }
    }

    bool isPlaying() override { return callback_ != nullptr; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return bufferSize_; }
    double getCurrentSampleRate() override { return rate_; }
    int getCurrentBitDepth() override { return 32; }
    juce::BigInteger getActiveOutputChannels() const override { return outputs_; }
    juce::BigInteger getActiveInputChannels() const override { return {}; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }

    /// Runs one audio callback on the calling thread and returns the peak of the left output.
    float runBlock()
    {
        std::vector<float> left(static_cast<std::size_t>(bufferSize_));
        std::vector<float> right(static_cast<std::size_t>(bufferSize_));
        std::array<float*, 2> outputs{left.data(), right.data()};
        if (callback_ != nullptr)
        {
            callback_->audioDeviceIOCallbackWithContext(nullptr, 0, outputs.data(), 2, bufferSize_,
                                                        {});
        }
        float peak = 0.0f;
        for (const float s : left)
        {
            peak = std::max(peak, std::abs(s));
        }
        return peak;
    }

private:
    FakeWorld& world_;
    juce::AudioIODeviceCallback* callback_ = nullptr;
    juce::BigInteger outputs_;
    double rate_ = 48'000.0;
    int bufferSize_ = 256;
    bool open_ = false;
};

class FakeDeviceType final : public juce::AudioIODeviceType
{
public:
    explicit FakeDeviceType(FakeWorld& world)
        : AudioIODeviceType("Fake")
        , world_(world)
    {
    }

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool wantInputNames) const override
    {
        return wantInputNames ? juce::StringArray{} : world_.devices;
    }
    int getDefaultDeviceIndex(bool /*forInput*/) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice* device, bool asInput) const override
    {
        return asInput || device == nullptr ? -1 : world_.devices.indexOf(device->getName());
    }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice(const juce::String& outputName,
                                      const juce::String& /*inputName*/) override
    {
        return world_.devices.contains(outputName) ? new FakeDevice(outputName, world_) : nullptr;
    }

    /// What a driver does on hot-plug.
    void devicesChanged() { callDeviceChangeListeners(); }

private:
    FakeWorld& world_;
};

} // namespace af::test
