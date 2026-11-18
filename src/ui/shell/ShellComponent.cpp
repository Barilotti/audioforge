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

juce::String ShellComponent::audioFilePatterns()
{
    return "*.wav;*.wave;*.bwf;*.rf64;*.bw64;*.w64";
}

bool ShellComponent::isSupportedAudioFile(const juce::File& file)
{
    const auto patterns = juce::StringArray::fromTokens(audioFilePatterns(), ";", "");
    for (const auto& pattern : patterns)
    {
        if (file.getFileName().matchesWildcard(pattern, true))
        {
            return true;
        }
    }
    return false;
}

bool ShellComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    // Every file is accepted so that an unsupported one gets an explanation, not silence.
    return !files.isEmpty() && onAudioFilesDropped != nullptr;
}

void ShellComponent::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/)
{
    juce::Array<juce::File> supported;
    juce::StringArray unsupported;
    for (const auto& path : files)
    {
        const juce::File file{path};
        if (isSupportedAudioFile(file))
        {
            supported.add(file);
        }
        else
        {
            unsupported.add(file.getFileName());
        }
    }
    if (!supported.isEmpty())
    {
        onAudioFilesDropped(supported);
    }
    if (!unsupported.isEmpty())
    {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::InfoIcon)
                .withTitle(TRANS("Unsupported file"))
                .withMessage(TRANS("This version opens WAV files (WAV, BWF, RF64, BW64, Wave64). "
                                   "AIFF, FLAC and MP3 support is planned.") +
                             "\n\n" + unsupported.joinIntoString("\n"))
                .withButton(TRANS("OK"))
                .withAssociatedComponent(this),
            nullptr);
    }
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
