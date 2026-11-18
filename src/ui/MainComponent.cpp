#include "ui/MainComponent.h"

#include "core/Version.h"

namespace af::ui
{

namespace
{
const juce::Colour backgroundColour{0xff1b1d21};
const juce::Colour textColour{0xffc9ccd1};
const juce::Colour secondaryTextColour{0xff7d828a};
} // namespace

MainComponent::MainComponent()
{
    setSize(1280, 800);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(backgroundColour);

    auto area = getLocalBounds().withSizeKeepingCentre(600, 80);

    g.setColour(textColour);
    g.setFont(juce::FontOptions{28.0f});
    g.drawText("AudioForge", area.removeFromTop(44), juce::Justification::centred);

    g.setColour(secondaryTextColour);
    g.setFont(juce::FontOptions{14.0f});
    g.drawText(juce::String{std::string{core::buildDescription()}}, area,
               juce::Justification::centred);
}

} // namespace af::ui
