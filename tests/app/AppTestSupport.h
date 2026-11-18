#pragma once

#include "FakeAudioDevices.h"
#include "app/DeviceService.h"
#include "app/Playback.h"
#include "assets/AudioBufferCache.h"
#include "core/Uuid.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace af::test
{

using app::DeviceEvent;
using app::DeviceService;

class TempSettings
{
public:
    TempSettings()
        : dir_(std::filesystem::temp_directory_path() /
               ("af-app-test-" + core::Uuid::generate().toString()))
    {
        std::filesystem::create_directories(dir_);
    }
    ~TempSettings()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }
    TempSettings(const TempSettings&) = delete;
    TempSettings& operator=(const TempSettings&) = delete;
    TempSettings(TempSettings&&) = delete;
    TempSettings& operator=(TempSettings&&) = delete;

    [[nodiscard]] std::filesystem::path file() const { return dir_ / "AudioDevice.xml"; }

    [[nodiscard]] std::string contents() const
    {
        std::ifstream in(file());
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }

private:
    std::filesystem::path dir_;
};

struct Events final : DeviceService::Listener
{
    std::vector<DeviceEvent> received;
    void deviceEvent(const DeviceEvent& event) override { received.push_back(event); }

    [[nodiscard]] int count(DeviceEvent::Kind kind) const
    {
        int n = 0;
        for (const auto& e : received)
        {
            n += e.kind == kind ? 1 : 0;
        }
        return n;
    }
};

inline assets::AudioBufferCache::Options smallCache()
{
    assets::AudioBufferCache::Options o;
    o.budget = assets::MemoryBudget::defaultsFor(4 * assets::gibibyte);
    o.decoders = assets::DecoderRegistry::withBuiltInDecoders();
    o.workerThreads = 1;
    return o;
}

/// A DeviceService on fake devices, with its listener attached before initialise(), and the
/// Playback that the application puts on top of it.
struct Harness
{
    FakeWorld world;
    FakeDeviceType* type = nullptr;
    Events events;
    std::unique_ptr<DeviceService> service;
    assets::AudioBufferCache cache{smallCache()};
    std::unique_ptr<app::Playback> playback;

    explicit Harness(const std::filesystem::path& settings,
                     juce::StringArray devices = {"Fake A", "Fake B"})
    {
        world.devices = std::move(devices);
        DeviceService::Options options;
        options.settingsFile = settings;
        options.timerHz = 0; // tests call refresh() themselves
        auto fake = std::make_unique<FakeDeviceType>(world);
        type = fake.get();
        options.deviceTypes.push_back(std::move(fake));
        service = std::make_unique<DeviceService>(std::move(options));
        service->addListener(&events);
        service->initialise();
        playback = std::make_unique<app::Playback>(*service, cache, 0);
    }

    ~Harness()
    {
        playback.reset();
        service->removeListener(&events);
    }

    Harness(const Harness&) = delete;
    Harness& operator=(const Harness&) = delete;
    Harness(Harness&&) = delete;
    Harness& operator=(Harness&&) = delete;

    [[nodiscard]] FakeDevice* device() const
    {
        return dynamic_cast<FakeDevice*>(service->deviceManager().getCurrentAudioDevice());
    }

    void choose(const juce::String& name, double rate, int buffer) const
    {
        auto setup = service->deviceManager().getAudioDeviceSetup();
        setup.outputDeviceName = name;
        setup.sampleRate = rate;
        setup.bufferSize = buffer;
        REQUIRE(service->deviceManager()
                    .setAudioDeviceSetup(setup, true)
                    .isEmpty()); // as the dialog does
        service->refresh();
    }

    void unplug(const juce::String& name)
    {
        world.devices.removeString(name);
        type->devicesChanged();
        service->refresh();
    }

    void plugIn(const juce::String& name)
    {
        world.devices.add(name);
        type->devicesChanged();
        service->refresh();
    }
};

} // namespace af::test
