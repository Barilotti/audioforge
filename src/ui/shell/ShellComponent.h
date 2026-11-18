#pragma once

#include "ui/shell/Panel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace af::ui
{

/// The main window layout (architecture section 11 of the spec):
/// left browser panel, centre timeline, right inspector, bottom transport and mixer,
/// separated by draggable resizer bars.
class ShellComponent final : public juce::Component, public juce::FileDragAndDropTarget
{
public:
    ShellComponent();
    ~ShellComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    [[nodiscard]] Panel& leftPanel() noexcept { return left_; }
    [[nodiscard]] Panel& centrePanel() noexcept { return centre_; }
    [[nodiscard]] Panel& rightPanel() noexcept { return right_; }
    [[nodiscard]] Panel& bottomPanel() noexcept { return bottom_; }

    /// Called with an audio file dropped anywhere on the window.
    std::function<void(const juce::File&)> onAudioFileDropped;

    // FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    /// File patterns of the formats AudioForge imports, for file choosers and drops.
    [[nodiscard]] static juce::String audioFilePatterns();

private:
    Panel left_;
    Panel centre_;
    Panel right_;
    Panel bottom_;

    // Vertical stack: top row / resizer / bottom panel.
    juce::StretchableLayoutManager rows_;
    juce::StretchableLayoutResizerBar rowResizer_{&rows_, 1, false};

    // Top row: left / resizer / centre / resizer / right.
    juce::StretchableLayoutManager columns_;
    juce::StretchableLayoutResizerBar leftResizer_{&columns_, 1, true};
    juce::StretchableLayoutResizerBar rightResizer_{&columns_, 3, true};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShellComponent)
};

} // namespace af::ui
