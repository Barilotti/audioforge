#pragma once

#include "app/DeviceService.h"
#include "assets/AudioBufferCache.h"
#include "engine/GraphCompiler.h"
#include "model/Project.h"

#include <juce_events/juce_events.h>

namespace af::app
{

/// Plays a looping 440 Hz tone through the whole engine path (cache, graph compiler, graph
/// hand-over, device). Used by the Audio Settings dialog to check a device, and for the manual
/// S1.5 checklist: changing device, rate or buffer while it plays must recover by itself.
class TestTone final : public DeviceService::Listener, private juce::Timer
{
public:
    explicit TestTone(DeviceService& devices);
    ~TestTone() override;

    TestTone(const TestTone&) = delete;
    TestTone& operator=(const TestTone&) = delete;
    TestTone(TestTone&&) = delete;
    TestTone& operator=(TestTone&&) = delete;

    void start();
    void stop();
    [[nodiscard]] bool isPlaying() const noexcept { return playing_; }

private:
    void deviceEvent(const DeviceEvent& event) override;
    void timerCallback() override;
    void publishGraph();

    DeviceService& devices_;
    assets::AudioBufferCache cache_;
    engine::GraphCompiler compiler_;
    model::Project project_;
    model::EventId event_;
    core::TimeTicks length_{};
    bool playing_ = false;
};

} // namespace af::app
