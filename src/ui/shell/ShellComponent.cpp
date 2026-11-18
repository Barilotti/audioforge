#include "ui/shell/ShellComponent.h"

#include "ui/theme/Theme.h"

#include <array>

namespace af::ui
{

ShellComponent::ShellComponent()
    : left_(TRANS("Browser"))
    , centre_(TRANS("Timeline"))
    , right_(TRANS("Inspector"))
    , bottom_(TRANS("Transport & Mixer"))
{
    left_.setContent(std::make_unique<PlaceholderContent>(
        TRANS("Project, Sample Browser and Recipe Library will appear here.")));
    centre_.setContent(std::make_unique<PlaceholderContent>(
        TRANS("Timeline with layers and waveforms will appear here.")));
    right_.setContent(
        std::make_unique<PlaceholderContent>(TRANS("Layer and clip parameters will appear here.")));
    bottom_.setContent(std::make_unique<PlaceholderContent>(
        TRANS("Transport controls, mixer and metering will appear here.")));

    const int bar = Theme::current().metrics.resizerThickness;

    // Sizes: minimum, maximum, preferred. Negative values are proportions of the available space.
    columns_.setItemLayout(0, 200, 600, 280);   // left
    columns_.setItemLayout(1, bar, bar, bar);   // resizer
    columns_.setItemLayout(2, 300, -1.0, -1.0); // centre takes the rest
    columns_.setItemLayout(3, bar, bar, bar);   // resizer
    columns_.setItemLayout(4, 220, 600, 300);   // right

    rows_.setItemLayout(0, 200, -1.0, -1.0); // top row takes the rest
    rows_.setItemLayout(1, bar, bar, bar);   // resizer
    rows_.setItemLayout(2, 120, 500, 200);   // bottom

    // All panels and bars are direct children: a resizer bar re-lays out its parent when dragged.
    addAndMakeVisible(left_);
    addAndMakeVisible(leftResizer_);
    addAndMakeVisible(centre_);
    addAndMakeVisible(rightResizer_);
    addAndMakeVisible(right_);
    addAndMakeVisible(rowResizer_);
    addAndMakeVisible(bottom_);
}

void ShellComponent::paint(juce::Graphics& g)
{
    g.fillAll(Theme::current().colours.windowBackground);
}

void ShellComponent::resized()
{
    auto bounds = getLocalBounds();

    // The top row is a layout slot without its own component (nullptr), filled by the columns.
    std::array<juce::Component*, 3> rowItems{nullptr, &rowResizer_, &bottom_};
    rows_.layOutComponents(rowItems.data(), static_cast<int>(rowItems.size()), bounds.getX(),
                           bounds.getY(), bounds.getWidth(), bounds.getHeight(), true, true);
    const int topHeight = rows_.getItemCurrentAbsoluteSize(0);

    std::array<juce::Component*, 5> columnItems{&left_, &leftResizer_, &centre_, &rightResizer_,
                                                &right_};
    columns_.layOutComponents(columnItems.data(), static_cast<int>(columnItems.size()),
                              bounds.getX(), bounds.getY(), bounds.getWidth(), topHeight, false,
                              true);
}

} // namespace af::ui
