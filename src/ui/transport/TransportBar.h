#pragma once

#include "app/DeviceService.h"
#include "app/Playback.h"
#include "ui/widgets/LevelMeter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace af::ui
{

/// Transport controls (Play/Pause, Stop, Import), playhead position, the imported file and the
/// master meter. Refreshed at screen rate.
class TransportBar final : public juce::Component
{
public:
    TransportBar(app::DeviceService& devices, app::Playback& playback,
                 std::function<void()> onImport);
    ~TransportBar() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /// "m:ss.mmm"; hours are added when needed.
    [[nodiscard]] static juce::String formatTime(double seconds);

private:
    void refresh();

    app::DeviceService& devices_;
    app::Playback& playback_;
    juce::TextButton playButton_;
    juce::TextButton stopButton_;
    juce::TextButton importButton_;
    juce::Label position_;
    juce::Label file_;
    juce::Label load_;
    LevelMeter meter_;
    juce::VBlankAttachment vblank_;
    double lastRefresh_ = 0.0;
    app::Playback::State lastState_ = app::Playback::State::empty;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

} // namespace af::ui
