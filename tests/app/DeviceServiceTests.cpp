#include "FakeAudioDevices.h"
#include "app/DeviceService.h"
#include "app/TestTone.h"
#include "core/Uuid.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace af;
using af::app::DeviceEvent;
using af::app::DeviceService;
using af::test::FakeDevice;
using af::test::FakeDeviceType;
using af::test::FakeWorld;

namespace
{

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

/// A DeviceService on fake devices, with its listener attached before initialise().
struct Harness
{
    FakeWorld world;
    FakeDeviceType* type = nullptr;
    Events events;
    std::unique_ptr<DeviceService> service;

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
    }

    ~Harness() { service->removeListener(&events); }

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

} // namespace

TEST_CASE("The default device opens and plays the engine output", "[app][devices]")
{
    const TempSettings settings;
    Harness h{settings.file()};

    const auto status = h.service->status();
    REQUIRE(status.open);
    REQUIRE(status.deviceName == "Fake A");
    REQUIRE(status.outputChannels == 2);
    REQUIRE(h.events.count(DeviceEvent::Kind::formatChanged) == 1);
    const auto spec = h.service->renderSpec();
    REQUIRE(spec.has_value());
    REQUIRE(spec->sampleRate == static_cast<core::SampleRate>(status.sampleRate));
    REQUIRE(spec->maxBlockFrames == status.bufferSize);

    REQUIRE(h.device()->runBlock() == 0.0f); // nothing published yet: silence
    app::TestTone tone{*h.service};
    tone.start();
    h.device()->runBlock();
    REQUIRE(h.device()->runBlock() > 0.05f);
    REQUIRE(h.service->engine().status().appliedGeneration > 0);
    tone.stop();
}

TEST_CASE("The user's device choice is saved and restored", "[app][devices]")
{
    const TempSettings settings;
    {
        Harness h{settings.file()};
        h.choose("Fake B", 96'000.0, 128);
        REQUIRE(settings.contents().find("Fake B") != std::string::npos);
    }
    Harness again{settings.file()};
    const auto status = again.service->status();
    REQUIRE(status.deviceName == "Fake B");
    REQUIRE(status.sampleRate == 96'000.0);
    REQUIRE(status.bufferSize == 128);
    REQUIRE_FALSE(status.usingFallback);
}

TEST_CASE("Unreadable settings fall back to the default device", "[app][devices]")
{
    const TempSettings settings;
    {
        std::ofstream out(settings.file());
        out << "<not valid xml";
    }
    Harness h{settings.file()};
    REQUIRE(h.service->status().deviceName == "Fake A");
    h.choose("Fake B", 48'000.0, 256);
    REQUIRE(settings.contents().find("<DEVICESETUP") != std::string::npos);
}

TEST_CASE("Changing rate or buffer while playing recompiles and keeps playing", "[app][devices]")
{
    const TempSettings settings;
    Harness h{settings.file()};
    app::TestTone tone{*h.service};
    tone.start();
    REQUIRE(h.device()->runBlock() >= 0.0f);

    for (const auto& [rate, buffer] :
         std::vector<std::pair<double, int>>{{96'000.0, 64}, {44'100.0, 512}})
    {
        const auto before = h.events.count(DeviceEvent::Kind::formatChanged);
        h.choose("Fake A", rate, buffer);
        REQUIRE(h.events.count(DeviceEvent::Kind::formatChanged) == before + 1);
        REQUIRE(h.events.received.back().status.sampleRate == rate);
        REQUIRE(h.service->engine().status().rejectedGraphs == 0);
        h.device()->runBlock();
        REQUIRE(h.device()->runBlock() > 0.05f); // the tone was recompiled for the new format
        REQUIRE(tone.isPlaying());
    }
}

TEST_CASE("Unplugging the chosen device stops playback; plugging it back reopens it",
          "[app][devices]")
{
    const TempSettings settings;
    Harness h{settings.file()};
    h.choose("Fake B", 48'000.0, 256);
    app::TestTone tone{*h.service};
    tone.start();
    h.device()->runBlock();
    REQUIRE(h.device()->runBlock() > 0.05f);

    h.unplug("Fake B");
    REQUIRE(h.events.count(DeviceEvent::Kind::deviceLost) == 1);
    REQUIRE_FALSE(h.service->engine().status().playing);
    REQUIRE_FALSE(tone.isPlaying());
    auto status = h.service->status();
    REQUIRE(status.open);
    REQUIRE(status.deviceName == "Fake A"); // JUCE opened a fallback
    REQUIRE(status.usingFallback);
    REQUIRE(status.preferredName == "Fake B");
    REQUIRE(h.device()->runBlock() == 0.0f);
    REQUIRE(settings.contents().find("Fake B") != std::string::npos); // the choice is kept

    h.plugIn("Fake B");
    REQUIRE(h.events.count(DeviceEvent::Kind::deviceRestored) == 1);
    status = h.service->status();
    REQUIRE(status.deviceName == "Fake B");
    REQUIRE_FALSE(status.usingFallback);
}

TEST_CASE("Losing every device leaves the application running silently", "[app][devices]")
{
    const TempSettings settings;
    Harness h{settings.file(), {"Fake A"}};
    app::TestTone tone{*h.service};
    tone.start();

    h.unplug("Fake A");
    REQUIRE(h.events.count(DeviceEvent::Kind::noDevice) == 1);
    REQUIRE_FALSE(h.service->status().open);
    REQUIRE_FALSE(h.service->renderSpec().has_value());
    REQUIRE(h.service->engine().status().appliedGeneration == 0); // graphs were taken back
    REQUIRE_FALSE(tone.isPlaying());

    h.plugIn("Fake A");
    REQUIRE(h.service->status().open);
    REQUIRE(h.service->status().deviceName == "Fake A");
}

TEST_CASE("Starting without any device works", "[app][devices]")
{
    const TempSettings settings;
    Harness h{settings.file(), {}};
    REQUIRE_FALSE(h.service->status().open);
    REQUIRE(h.events.count(DeviceEvent::Kind::noDevice) == 1);
    app::TestTone tone{*h.service};
    tone.start(); // nothing to play on, must not crash
    tone.stop();
}
