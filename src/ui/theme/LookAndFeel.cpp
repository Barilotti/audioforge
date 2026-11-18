#include "ui/theme/LookAndFeel.h"

#include "ui/theme/Theme.h"

namespace af::ui
{

namespace
{

juce::LookAndFeel_V4::ColourScheme makeColourScheme(const Theme& theme)
{
    const auto& c = theme.colours;
    return {c.windowBackground,  // windowBackground
            c.panelBackground,   // widgetBackground
            c.controlBackground, // menuBackground
            c.controlOutline,    // outline
            c.textPrimary,       // defaultText
            c.controlBackground, // defaultFill
            c.textPrimary,       // highlightedText
            c.selection,         // highlightedFill
            c.textPrimary};      // menuText
}

} // namespace

LookAndFeel::LookAndFeel()
    : juce::LookAndFeel_V4(makeColourScheme(Theme::current()))
{
    const auto& c = Theme::current().colours;
    setColour(juce::ResizableWindow::backgroundColourId, c.windowBackground);
    setColour(juce::DocumentWindow::textColourId, c.textPrimary);
    setColour(juce::TextButton::buttonColourId, c.controlBackground);
    setColour(juce::TextButton::textColourOffId, c.textPrimary);
    setColour(juce::TextButton::buttonOnColourId, c.accent);
    setColour(juce::TextButton::textColourOnId, c.accentText);
    setColour(juce::Label::textColourId, c.textPrimary);
    setColour(juce::PopupMenu::backgroundColourId, c.controlBackground);
    setColour(juce::PopupMenu::textColourId, c.textPrimary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, c.selection);
    setColour(juce::PopupMenu::highlightedTextColourId, c.textPrimary);
    setColour(juce::HyperlinkButton::textColourId, c.accent);
}

void LookAndFeel::drawStretchableLayoutResizerBar(juce::Graphics& g, int /*width*/, int /*height*/,
                                                  bool /*isVertical*/, bool isMouseOver,
                                                  bool isMouseDragging)
{
    const auto& c = Theme::current().colours;
    g.fillAll(isMouseOver || isMouseDragging ? c.resizerHover : c.resizer);
}

void LookAndFeel::drawMenuBarBackground(juce::Graphics& g, int width, int height,
                                        bool /*isMouseOverBar*/,
                                        juce::MenuBarComponent& /*menuBar*/)
{
    const auto& c = Theme::current().colours;
    g.fillAll(c.panelHeader);
    g.setColour(c.border);
    g.fillRect(0, height - 1, width, 1);
}

int LookAndFeel::getMenuBarItemWidth(juce::MenuBarComponent& menuBar, int itemIndex,
                                     const juce::String& itemText)
{
    // Leave extra room: translated menu titles are often longer than the English ones.
    return LookAndFeel_V4::getMenuBarItemWidth(menuBar, itemIndex, itemText) + 8;
}

} // namespace af::ui
