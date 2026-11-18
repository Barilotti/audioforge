#include "ui/settings/AudioSettingsComponent.h"

#include "ui/theme/Theme.h"

namespace af::ui
{

namespace
{

juce::String fromStd(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str());
}

juce::Component::SafePointer<juce::DialogWindow>& openDialog()
{
    static juce::Component::SafePointer<juce::DialogWindow> dialog;
    return dialog;
}

} // namespace

AudioSettingsComponent::AudioSettingsComponent(app::DeviceService& devices)
    : devices_(devices)
    , testTone_(devices)
    , selector_(devices.deviceManager(), 0, 0, 1, 2, false, false, true, false)
{
    addAndMakeVisible(selector_);

    testToneButton_.setButtonText(TRANS("Play test tone"));
    testToneButton_.setClickingTogglesState(true);
    testToneButton_.onClick = [this]
    {
        if (testToneButton_.getToggleState())
        {
            testTone_.start();
        }
        else
        {
            testTone_.stop();
        }
    };
    addAndMakeVisible(testToneButton_);

    status_.setJustificationType(juce::Justification::topLeft);
    status_.setColour(juce::Label::textColourId, Theme::current().colours.textSecondary);
    status_.setFont(juce::FontOptions{Theme::current().fontSizes.small});
    addAndMakeVisible(status_);

    closeButton_.setButtonText(TRANS("Close"));
    closeButton_.onClick = [this]
    {
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
        {
            dialog->exitModalState(0);
        }
    };
    addAndMakeVisible(closeButton_);

    setSize(560, 520);
    updateStatus();
    startTimerHz(4);
}

AudioSettingsComponent::~AudioSettingsComponent()
{
    stopTimer();
    testTone_.stop();
}

void AudioSettingsComponent::paint(juce::Graphics& g)
{
    g.fillAll(Theme::current().colours.panelBackground);
}

void AudioSettingsComponent::resized()
{
    auto area = getLocalBounds().reduced(16);
    auto buttons = area.removeFromBottom(28);
    closeButton_.setBounds(buttons.removeFromRight(100));
    buttons.removeFromRight(8);
    testToneButton_.setBounds(buttons.removeFromLeft(140));
    area.removeFromBottom(12);
    status_.setBounds(area.removeFromBottom(72));
    area.removeFromBottom(8);
    selector_.setBounds(area);
}

void AudioSettingsComponent::timerCallback()
{
    updateStatus();
    if (testToneButton_.getToggleState() != testTone_.isPlaying())
    {
        testToneButton_.setToggleState(testTone_.isPlaying(),
                                       juce::dontSendNotification); // stopped by device loss
    }
}

void AudioSettingsComponent::updateStatus()
{
    const auto s = devices_.status();
    const auto engine = devices_.engine().status();
    juce::String text;
    if (!s.open)
    {
        text << TRANS("No audio output device is open.");
        if (!s.error.empty())
        {
            text << "\n" << fromStd(s.error);
        }
    }
    else
    {
        const double latencyMs =
            s.sampleRate > 0.0 ? 1000.0 * (s.bufferSize + s.outputLatency) / s.sampleRate : 0.0;
        text << fromStd(s.typeName) << ": " << fromStd(s.deviceName) << "\n"
             << juce::String(s.sampleRate, 0) << " Hz, " << s.bufferSize << " " << TRANS("samples")
             << ", " << TRANS("output latency") << " " << juce::String(latencyMs, 1) << " ms";
        if (s.xruns >= 0)
        {
            text << ", " << TRANS("dropouts") << ": " << s.xruns;
        }
        if (s.usingFallback)
        {
            text << "\n"
                 << TRANS("The chosen device is not connected:") << " " << fromStd(s.preferredName);
        }
    }
    text << "\n"
         << TRANS("Engine") << ": " << TRANS("graph") << " "
         << static_cast<juce::int64>(engine.appliedGeneration) << ", " << TRANS("deferred swaps")
         << " " << static_cast<juce::int64>(engine.deferredSwaps) << ", "
         << TRANS("rejected graphs") << " " << static_cast<juce::int64>(engine.rejectedGraphs);
    status_.setText(text, juce::dontSendNotification);
}

void AudioSettingsComponent::show(app::DeviceService& devices, juce::Component* centreAround)
{
    if (auto* existing = openDialog().getComponent())
    {
        existing->toFront(true);
        return;
    }
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new AudioSettingsComponent(devices));
    options.dialogTitle = TRANS("Audio Settings");
    options.dialogBackgroundColour = Theme::current().colours.panelBackground;
    options.componentToCentreAround = centreAround;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    openDialog() = options.launchAsync();
}

} // namespace af::ui
