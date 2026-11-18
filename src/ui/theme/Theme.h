#pragma once

#include <juce_graphics/juce_graphics.h>

namespace af::ui
{

/// Design tokens: the single place for colours, sizes and font sizes. Components never use
/// literal colours or magic sizes; they read them from the current theme.
struct Theme
{
    struct Colours
    {
        juce::Colour windowBackground{0xff16181b};
        juce::Colour panelBackground{0xff1d2024};
        juce::Colour panelHeader{0xff23272c};
        juce::Colour border{0xff2e3238};
        juce::Colour resizer{0xff16181b};
        juce::Colour resizerHover{0xff3a4048};
        juce::Colour textPrimary{0xffd4d7dc};
        juce::Colour textSecondary{0xff8a9099};
        juce::Colour textDisabled{0xff5a6068};
        juce::Colour accent{0xffe0a030};
        juce::Colour accentText{0xff16181b};
        juce::Colour controlBackground{0xff2a2e34};
        juce::Colour controlOutline{0xff3a4048};
        juce::Colour selection{0xff3d5a80};
        juce::Colour error{0xffe05a4f};
    } colours;

    struct Metrics
    {
        int panelHeaderHeight = 28;
        int panelPadding = 8;
        int resizerThickness = 4;
        int menuBarHeight = 24;
        float cornerRadius = 3.0f;
    } metrics;

    struct FontSizes
    {
        float small = 12.0f;
        float normal = 14.0f;
        float title = 13.0f;
        float heading = 22.0f;
    } fontSizes;

    /// The theme in use. Phase 1 has one dark theme.
    [[nodiscard]] static const Theme& current();
};

} // namespace af::ui
