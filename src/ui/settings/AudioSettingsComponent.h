#pragma once

#include "app/DeviceService.h"
#include "app/TestTone.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace af::ui
{

/// Audio Settings dialog: output device, sample rate and buffer size (WASAPI on Windows), a
/// live status line with engine counters, and a test tone that runs through the full engine.
class AudioSettingsComponent final : public juce::Component, private juce::Timer
{
public:
    explicit AudioSettingsComponent(app::DeviceService& devices);
    ~AudioSettingsComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /// Opens the dialog without blocking; only one instance at a time.
    static void show(app::DeviceService& devices, juce::Component* centreAround);

private:
    void timerCallback() override;
    void updateStatus();

    app::DeviceService& devices_;
    app::TestTone testTone_;
    juce::AudioDeviceSelectorComponent selector_;
    juce::TextButton testToneButton_;
    juce::Label status_;
    juce::TextButton closeButton_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsComponent)
};

} // namespace af::ui
