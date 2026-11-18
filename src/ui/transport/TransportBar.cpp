#include "ui/transport/TransportBar.h"

#include "ui/theme/Theme.h"

#include <cmath>

namespace af::ui
{

namespace
{

juce::String fromStd(const std::string& text)
{
    return juce::String::fromUTF8(text.c_str());
}

void setUpButton(juce::TextButton& button, const juce::String& text, const juce::String& tooltip)
{
    button.setButtonText(text);
    button.setTooltip(tooltip);
    button.setWantsKeyboardFocus(false); // Space belongs to the transport, not to a button
}

} // namespace

TransportBar::TransportBar(app::DeviceService& devices, app::Playback& playback,
                           std::function<void()> onImport)
    : devices_(devices)
    , playback_(playback)
    , vblank_(this, [this] { refresh(); })
{
    setUpButton(playButton_, TRANS("Play"),
                TRANS("Play or pause (Space: play/stop, Shift+Space: play/pause)"));
    playButton_.onClick = [this] { playback_.transport().togglePlayPause(); };
    setUpButton(stopButton_, TRANS("Stop"), TRANS("Stop and return to the start position"));
    stopButton_.onClick = [this] { playback_.transport().stop(); };
    setUpButton(importButton_, TRANS("Import..."), TRANS("Import an audio file (Ctrl+I)"));
    importButton_.onClick = std::move(onImport);

    const auto& theme = Theme::current();
    position_.setFont(juce::FontOptions{theme.fontSizes.heading}.withStyle("Bold"));
    position_.setColour(juce::Label::textColourId, theme.colours.textPrimary);
    position_.setJustificationType(juce::Justification::centredLeft);
    file_.setFont(juce::FontOptions{theme.fontSizes.normal});
    file_.setColour(juce::Label::textColourId, theme.colours.textSecondary);
    file_.setJustificationType(juce::Justification::centredLeft);

    load_.setFont(juce::FontOptions{theme.fontSizes.small});
    load_.setColour(juce::Label::textColourId, theme.colours.textSecondary);
    load_.setJustificationType(juce::Justification::centredRight);
    load_.setTooltip(TRANS("Audio callback load and dropouts reported by the device since it "
                           "was opened"));

    meter_.onClipReset = [this] { devices_.engine().meter().resetClip(); };

    for (juce::Component* c : std::initializer_list<juce::Component*>{
             &playButton_, &stopButton_, &importButton_, &position_, &file_, &load_, &meter_})
    {
        addAndMakeVisible(c);
    }
    refresh();
}

juce::String TransportBar::formatTime(double seconds)
{
    const auto totalMs = static_cast<juce::int64>(std::llround(std::max(seconds, 0.0) * 1000.0));
    const auto ms = totalMs % 1000;
    const auto totalSeconds = totalMs / 1000;
    const auto s = totalSeconds % 60;
    const auto totalMinutes = totalSeconds / 60;
    const auto m = totalMinutes % 60;
    const auto h = totalMinutes / 60;
    juce::String text;
    if (h > 0)
    {
        text << h << ":" << juce::String(m).paddedLeft('0', 2);
    }
    else
    {
        text << m;
    }
    text << ":" << juce::String(s).paddedLeft('0', 2) << "." << juce::String(ms).paddedLeft('0', 3);
    return text;
}

void TransportBar::paint(juce::Graphics& g)
{
    g.fillAll(Theme::current().colours.panelBackground);
}

void TransportBar::resized()
{
    auto area = getLocalBounds().reduced(Theme::current().metrics.panelPadding);
    auto top = area.removeFromTop(36);
    playButton_.setBounds(top.removeFromLeft(80));
    top.removeFromLeft(6);
    stopButton_.setBounds(top.removeFromLeft(80));
    top.removeFromLeft(16);
    position_.setBounds(top.removeFromLeft(300));
    importButton_.setBounds(top.removeFromRight(100));
    top.removeFromRight(12);
    load_.setBounds(top.removeFromRight(220));
    area.removeFromTop(8);
    auto bottom = area.removeFromTop(40);
    meter_.setBounds(bottom.removeFromRight(juce::jmin(420, bottom.getWidth() / 2)));
    bottom.removeFromRight(16);
    file_.setBounds(bottom);
}

void TransportBar::refresh()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const double dt = lastRefresh_ > 0.0 ? juce::jlimit(0.0, 0.5, now - lastRefresh_) : 0.0;
    lastRefresh_ = now;

    auto& transport = playback_.transport();
    const auto status = playback_.status();
    const auto spec = devices_.renderSpec();

    playButton_.setButtonText(transport.isPlaying() ? TRANS("Pause") : TRANS("Play"));

    juce::String positionText = "-:--.---";
    if (spec)
    {
        positionText = formatTime(static_cast<double>(transport.position()) /
                                  static_cast<double>(spec->sampleRate));
    }
    if (status.testTone)
    {
        positionText << "  " << TRANS("(test tone)");
    }
    else if (status.lengthSeconds > 0.0)
    {
        positionText << " / " << formatTime(status.lengthSeconds);
    }
    position_.setText(positionText, juce::dontSendNotification);

    juce::String fileText;
    juce::Colour fileColour = Theme::current().colours.textSecondary;
    switch (status.state)
    {
    case app::Playback::State::empty:
        fileText = TRANS("No file. Use File > Import Audio... or drop a WAV file here.");
        break;
    case app::Playback::State::loading:
        fileText << TRANS("Loading") << " " << fromStd(status.loadingName) << " ("
                 << juce::roundToInt(status.progress * 100.0f) << "%)";
        break;
    case app::Playback::State::ready:
        fileText = fromStd(status.fileName);
        if (status.truncated)
        {
            fileText << "  " << TRANS("(the file is truncated: only the frames present play)");
        }
        break;
    case app::Playback::State::failed:
        fileText << TRANS("Cannot import") << " " << fromStd(status.loadingName) << ": "
                 << fromStd(status.error);
        fileColour = Theme::current().colours.error;
        break;
    }
    if (!spec)
    {
        fileText << "  " << TRANS("(no audio device)");
    }
    file_.setText(fileText, juce::dontSendNotification);
    if (status.state == app::Playback::State::failed && lastState_ != status.state)
    {
        auto message =
            TRANS("AudioForge cannot open \"%1\".").replace("%1", fromStd(status.loadingName));
        message << "\n\n" << fromStd(status.error);
        if (!status.fileName.empty())
        {
            message << "\n\n"
                    << TRANS("The previous file stays loaded:") << " " << fromStd(status.fileName);
        }
        juce::AlertWindow::showAsync(juce::MessageBoxOptions()
                                         .withIconType(juce::MessageBoxIconType::WarningIcon)
                                         .withTitle(TRANS("Import failed"))
                                         .withMessage(message)
                                         .withButton(TRANS("OK"))
                                         .withAssociatedComponent(this),
                                     nullptr);
    }
    lastState_ = status.state;
    file_.setColour(juce::Label::textColourId, fileColour);

    juce::String loadText;
    if (spec)
    {
        loadText << TRANS("CPU") << " "
                 << juce::roundToInt(devices_.deviceManager().getCpuUsage() * 100.0) << "%";
        if (const auto xruns = devices_.deviceManager().getXRunCount(); xruns >= 0)
        {
            loadText << "   " << TRANS("dropouts") << " " << xruns;
        }
    }
    load_.setText(loadText, juce::dontSendNotification);

    auto reading = devices_.engine().meter().read();
    if (!spec)
    {
        reading.rms = {}; // nothing runs: do not show the last value forever
    }
    meter_.setLevels(reading.peak, reading.rms, reading.clipped, dt);
}

} // namespace af::ui
