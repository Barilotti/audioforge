#pragma once

#include "ui/timeline/TimelineGeometry.h"
#include "ui/timeline/TimelineModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace af::ui
{

/// Draws the timeline: ruler, layer headers, lanes with the visible clips only (virtualized by
/// layer row and by clip time), and the playhead. Separate from the component so that tests
/// and benchmarks can paint into an image.
struct TimelineLayout
{
    static constexpr int rulerHeight = 28;
    static constexpr int headerWidth = 168;
    static constexpr int layerHeight = 72;

    juce::Rectangle<int> ruler;   ///< Above the lanes, same horizontal extent.
    juce::Rectangle<int> headers; ///< Left of the lanes.
    juce::Rectangle<int> lanes;
    int verticalScroll = 0; ///< Pixels of the layer list scrolled out above the lanes.

    [[nodiscard]] static TimelineLayout forArea(juce::Rectangle<int> area);
    [[nodiscard]] int contentHeight(std::size_t layerCount) const noexcept;
    /// Layer rows that intersect the lanes: [first, last).
    [[nodiscard]] std::pair<std::size_t, std::size_t> visibleLayers(std::size_t count) const;
    [[nodiscard]] juce::Rectangle<int> rowBounds(std::size_t layer) const;
};

class TimelinePainter
{
public:
    /// Paints everything; `playhead` in seconds, or nullopt to hide it. Returns the number of
    /// clips drawn (for tests of the virtualization).
    static int paint(juce::Graphics& g, const TimelineLayout& layout,
                     const TimelineGeometry& geometry, const TimelineModel& model,
                     std::optional<double> playhead);

    static void paintRuler(juce::Graphics& g, const TimelineLayout& layout,
                           const TimelineGeometry& geometry);
    static void paintPlayhead(juce::Graphics& g, const TimelineLayout& layout,
                              const TimelineGeometry& geometry, double seconds);

    /// Ruler label for a time, with as many decimals as the step needs.
    [[nodiscard]] static juce::String formatRulerTime(double seconds, double step);
};

} // namespace af::ui
