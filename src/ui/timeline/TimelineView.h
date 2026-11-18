#pragma once

#include "app/DeviceService.h"
#include "app/Playback.h"
#include "ui/timeline/TimelineGeometry.h"
#include "ui/timeline/TimelineModel.h"
#include "ui/timeline/TimelinePainter.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace af::ui
{

/// The timeline panel: ruler, layer rows with clips, playhead in sync with the audio, zoom and
/// scrolling.
///
/// Mouse: click or drag on the ruler moves the playhead; double-click on the ruler fits the
/// whole event. Wheel scrolls the layers, Shift+wheel (or a horizontal wheel) scrolls in time,
/// Ctrl+wheel zooms around the mouse.
class TimelineView final : public juce::Component, private juce::ScrollBar::Listener
{
public:
    TimelineView(app::DeviceService& devices, app::Playback& playback);
    ~TimelineView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;

    [[nodiscard]] const TimelineGeometry& geometry() const noexcept { return geometry_; }

private:
    void scrollBarMoved(juce::ScrollBar* bar, double newRangeStart) override;
    void refresh();
    void updateScrollBars();
    void seekToX(int x);
    [[nodiscard]] std::optional<double> playheadSeconds() const;
    [[nodiscard]] juce::Rectangle<int> playheadStrip(double seconds) const;

    app::DeviceService& devices_;
    app::Playback& playback_;
    TimelineGeometry geometry_;
    TimelineLayout layout_;
    TimelineModel model_;
    std::uint64_t revision_ = 0;
    bool fitted_ = false;
    unsigned assetPolls_ = 0;
    std::optional<double> shownPlayhead_;
    juce::ScrollBar horizontal_{false};
    juce::ScrollBar vertical_{true};
    juce::VBlankAttachment vblank_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineView)
};

} // namespace af::ui
