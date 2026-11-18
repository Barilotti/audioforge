#include "ui/shell/AboutComponent.h"

#include "core/Version.h"
#include "ui/theme/Theme.h"

namespace af::ui
{

namespace
{

juce::String toJuceString(std::string_view text)
{
    return juce::String::fromUTF8(text.data(), static_cast<int>(text.size()));
}

} // namespace

AboutComponent::AboutComponent()
{
    closeButton_.setButtonText(TRANS("Close"));
    closeButton_.onClick = [this]
    {
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
        {
            dialog->exitModalState(0);
        }
    };
    addAndMakeVisible(closeButton_);
    setSize(420, 220);
}

void AboutComponent::paint(juce::Graphics& g)
{
    const auto& theme = Theme::current();
    g.fillAll(theme.colours.panelBackground);

    auto area = getLocalBounds().reduced(24);

    g.setColour(theme.colours.textPrimary);
    g.setFont(juce::FontOptions{theme.fontSizes.heading});
    g.drawText("AudioForge", area.removeFromTop(32), juce::Justification::centredLeft);

    g.setColour(theme.colours.textSecondary);
    g.setFont(juce::FontOptions{theme.fontSizes.normal});
    g.drawText(TRANS("Sound design workstation"), area.removeFromTop(22),
               juce::Justification::centredLeft);

    area.removeFromTop(12);
    g.setFont(juce::FontOptions{theme.fontSizes.small});
    g.drawText(TRANS("Version") + " " + toJuceString(core::buildDescription()),
               area.removeFromTop(20), juce::Justification::centredLeft);
    g.drawText(toJuceString(core::juceVersionString()), area.removeFromTop(20),
               juce::Justification::centredLeft);
}

void AboutComponent::resized()
{
    closeButton_.setBounds(getLocalBounds().reduced(24).removeFromBottom(28).removeFromRight(100));
}

void AboutComponent::show(juce::Component* centreAround)
{
    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(new AboutComponent());
    options.dialogTitle = TRANS("About AudioForge");
    options.dialogBackgroundColour = Theme::current().colours.panelBackground;
    options.componentToCentreAround = centreAround;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

} // namespace af::ui
