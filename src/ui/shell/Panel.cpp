#include "ui/shell/Panel.h"

#include "ui/theme/Theme.h"

namespace af::ui
{

Panel::Panel(juce::String title)
    : title_(std::move(title))
{
    setTitle(title_); // accessibility name
}

void Panel::setContent(std::unique_ptr<juce::Component> content)
{
    if (content_ != nullptr)
    {
        removeChildComponent(content_.get());
    }
    content_ = std::move(content);
    if (content_ != nullptr)
    {
        addAndMakeVisible(*content_);
    }
    resized();
}

void Panel::paint(juce::Graphics& g)
{
    const auto& theme = Theme::current();
    auto bounds = getLocalBounds();

    g.fillAll(theme.colours.panelBackground);

    auto header = bounds.removeFromTop(theme.metrics.panelHeaderHeight);
    g.setColour(theme.colours.panelHeader);
    g.fillRect(header);
    g.setColour(theme.colours.border);
    g.fillRect(header.removeFromBottom(1));

    g.setColour(theme.colours.textSecondary);
    g.setFont(juce::FontOptions{theme.fontSizes.title}.withStyle("Bold"));
    g.drawFittedText(title_.toUpperCase(), header.reduced(theme.metrics.panelPadding, 0),
                     juce::Justification::centredLeft, 1);
}

void Panel::resized()
{
    if (content_ != nullptr)
    {
        content_->setBounds(
            getLocalBounds().withTrimmedTop(Theme::current().metrics.panelHeaderHeight));
    }
}

PlaceholderContent::PlaceholderContent(juce::String text)
    : text_(std::move(text))
{
}

void PlaceholderContent::paint(juce::Graphics& g)
{
    const auto& theme = Theme::current();
    g.setColour(theme.colours.textDisabled);
    g.setFont(juce::FontOptions{theme.fontSizes.small});
    g.drawFittedText(text_, getLocalBounds().reduced(theme.metrics.panelPadding),
                     juce::Justification::centred, 3);
}

} // namespace af::ui
