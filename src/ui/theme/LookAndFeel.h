#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace af::ui
{

/// AudioForge's dark look and feel, built from Theme tokens.
class LookAndFeel final : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    ~LookAndFeel() override = default;

    void drawStretchableLayoutResizerBar(juce::Graphics& g, int width, int height, bool isVertical,
                                         bool isMouseOver, bool isMouseDragging) override;

    void drawMenuBarBackground(juce::Graphics& g, int width, int height, bool isMouseOverBar,
                               juce::MenuBarComponent& menuBar) override;

    int getMenuBarItemWidth(juce::MenuBarComponent& menuBar, int itemIndex,
                            const juce::String& itemText) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LookAndFeel)
};

} // namespace af::ui
