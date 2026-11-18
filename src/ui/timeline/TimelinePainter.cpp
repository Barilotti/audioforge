#include "ui/timeline/TimelinePainter.h"

#include "ui/theme/Theme.h"

#include <algorithm>
#include <cmath>

namespace af::ui
{

TimelineLayout TimelineLayout::forArea(juce::Rectangle<int> area)
{
    TimelineLayout layout;
    auto top = area.removeFromTop(rulerHeight);
    layout.headers = area.removeFromLeft(headerWidth);
    top.removeFromLeft(headerWidth);
    layout.ruler = top;
    layout.lanes = area;
    return layout;
}

int TimelineLayout::contentHeight(std::size_t layerCount) const noexcept
{
    return static_cast<int>(layerCount) * layerHeight;
}

std::pair<std::size_t, std::size_t> TimelineLayout::visibleLayers(std::size_t count) const
{
    const auto first = static_cast<std::size_t>(std::max(0, verticalScroll / layerHeight));
    const auto last = static_cast<std::size_t>(
        std::max(0, (verticalScroll + lanes.getHeight() + layerHeight - 1) / layerHeight));
    return {std::min(first, count), std::min(last, count)};
}

juce::Rectangle<int> TimelineLayout::rowBounds(std::size_t layer) const
{
    return {lanes.getX(), lanes.getY() + static_cast<int>(layer) * layerHeight - verticalScroll,
            lanes.getWidth(), layerHeight};
}

juce::String TimelinePainter::formatRulerTime(double seconds, double step)
{
    const int decimals =
        step >= 1.0 ? 0 : std::min(3, static_cast<int>(std::ceil(-std::log10(step) - 1e-9)));
    const double rounded =
        std::round(seconds * std::pow(10.0, decimals)) / std::pow(10.0, decimals);
    const auto minutes = static_cast<int>(rounded / 60.0);
    const double rest = rounded - minutes * 60.0;
    juce::String text;
    text << minutes << ":";
    auto secondsText = juce::String(rest, decimals);
    if (rest < 10.0)
    {
        secondsText = "0" + secondsText;
    }
    text << secondsText;
    return text;
}

void TimelinePainter::paintRuler(juce::Graphics& g, const TimelineLayout& layout,
                                 const TimelineGeometry& geometry)
{
    const auto& colours = Theme::current().colours;
    const auto area = layout.ruler;
    g.setColour(colours.panelHeader);
    g.fillRect(area);
    g.setColour(colours.border);
    g.fillRect(area.withTop(area.getBottom() - 1));

    const double step = geometry.rulerStep();
    const double minor = step / 5.0;
    const auto first = std::floor(geometry.viewStart() / minor);
    g.setFont(juce::FontOptions{11.0f});
    for (double i = first;; i += 1.0)
    {
        const double t = i * minor;
        const double x = area.getX() + geometry.xForTime(t);
        if (x > area.getRight())
        {
            break;
        }
        if (x < area.getX())
        {
            continue;
        }
        const bool major = std::fmod(std::round(i), 5.0) == 0.0;
        const int xi = juce::roundToInt(x);
        g.setColour(major ? colours.textSecondary : colours.controlOutline);
        g.drawVerticalLine(xi, static_cast<float>(area.getBottom() - (major ? 10 : 5)),
                           static_cast<float>(area.getBottom()));
        if (major)
        {
            g.setColour(colours.textSecondary);
            g.drawText(formatRulerTime(t, step), xi + 3, area.getY() + 2, 90, 14,
                       juce::Justification::centredLeft, false);
        }
    }
}

void TimelinePainter::paintPlayhead(juce::Graphics& g, const TimelineLayout& layout,
                                    const TimelineGeometry& geometry, double seconds)
{
    const double x = layout.lanes.getX() + geometry.xForTime(seconds);
    if (x < layout.lanes.getX() - 1 || x > layout.lanes.getRight() + 1)
    {
        return;
    }
    const auto& colours = Theme::current().colours;
    g.setColour(colours.accent);
    g.fillRect(juce::Rectangle<float>{
        static_cast<float>(x) - 0.5f, static_cast<float>(layout.ruler.getY()), 1.5f,
        static_cast<float>(layout.lanes.getBottom() - layout.ruler.getY())});
    juce::Path head;
    const auto top = static_cast<float>(layout.ruler.getY());
    head.addTriangle(static_cast<float>(x) - 6.0f, top, static_cast<float>(x) + 6.0f, top,
                     static_cast<float>(x), top + 8.0f);
    g.fillPath(head);
}

void TimelinePainter::paintWaveform(juce::Graphics& g, juce::Rectangle<float> area,
                                    const TimelineClip& clip, const TimelineGeometry& geometry,
                                    int laneX)
{
    constexpr int maxBands = 8;
    const int channels = std::min(clip.channels(), maxBands);
    if (channels <= 0 || area.getHeight() < 4.0f || area.getWidth() < 1.0f)
    {
        return;
    }
    const auto& colours = Theme::current().colours;
    const float bandHeight = area.getHeight() / static_cast<float>(channels);
    // Only the columns inside the area being repainted (the playhead strip, for example).
    const auto dirty = g.getClipBounds();
    const auto x0 = std::max(static_cast<int>(std::floor(area.getX())), dirty.getX());
    const auto x1 = std::min(static_cast<int>(std::ceil(area.getRight())), dirty.getRight());
    if (x1 <= x0)
    {
        return;
    }

    juce::RectangleList<float> columns;
    columns.ensureStorageAllocated((x1 - x0) * channels);
    for (int ch = 0; ch < channels; ++ch)
    {
        const float top = area.getY() + static_cast<float>(ch) * bandHeight;
        const float centre = top + bandHeight * 0.5f;
        const float scale = bandHeight * 0.5f;
        g.setColour(colours.selection.brighter(0.2f).withAlpha(0.5f));
        g.fillRect(juce::Rectangle<float>{area.getX(), centre, area.getWidth(), 1.0f});
        for (int x = x0; x < x1; ++x)
        {
            const double t0 = geometry.timeForX(x - laneX);
            const double t1 = geometry.timeForX(x + 1 - laneX);
            const auto range = clip.columnRange(ch, t0, t1);
            if (!range)
            {
                continue;
            }
            const float yTop = centre - std::clamp(range->max, -1.0f, 1.0f) * scale;
            const float yBottom = centre - std::clamp(range->min, -1.0f, 1.0f) * scale;
            columns.addWithoutMerging(
                {static_cast<float>(x), yTop, 1.0f, std::max(1.0f, yBottom - yTop)});
        }
    }
    g.setColour(colours.textPrimary.withAlpha(0.85f));
    g.fillRectList(columns);
}

int TimelinePainter::paint(juce::Graphics& g, const TimelineLayout& layout,
                           const TimelineGeometry& geometry, const TimelineModel& model,
                           std::optional<double> playhead)
{
    const auto& theme = Theme::current();
    const auto& colours = theme.colours;
    g.setColour(colours.windowBackground);
    g.fillRect(layout.lanes);
    g.setColour(colours.panelBackground);
    g.fillRect(layout.headers);
    g.fillRect(layout.ruler.withX(layout.headers.getX()).withWidth(layout.headers.getWidth()));

    paintRuler(g, layout, geometry);

    int drawn = 0;
    const auto [first, last] = layout.visibleLayers(model.layers.size());
    const auto repainted = g.getClipBounds().getIntersection(layout.lanes);
    const double viewStart =
        std::max(geometry.viewStart(), geometry.timeForX(repainted.getX() - layout.lanes.getX()));
    const double viewEnd =
        std::min(geometry.viewEnd(), geometry.timeForX(repainted.getRight() - layout.lanes.getX()));
    for (auto index = first; index < last; ++index)
    {
        const auto& layer = model.layers[index];
        const auto row = layout.rowBounds(index);
        const auto dirty = g.getClipBounds();
        if (!row.withX(layout.headers.getX()).withRight(layout.lanes.getRight()).intersects(dirty))
        {
            continue; // nothing of this row is repainted
        }
        {
            const juce::Graphics::ScopedSaveState state{g};
            g.reduceClipRegion(layout.lanes);
            g.setColour(index % 2 == 0 ? colours.panelBackground
                                       : colours.panelBackground.brighter(0.03f));
            g.fillRect(row);
            g.setColour(colours.border);
            g.fillRect(row.withTop(row.getBottom() - 1));

            // Clips are sorted by start; none is longer than maxClipLength. So the first clip
            // that can reach into the view starts at or after viewStart - maxClipLength.
            auto it = std::lower_bound(layer.clips.begin(), layer.clips.end(),
                                       viewStart - layer.maxClipLength,
                                       [](const TimelineClip& c, double t) { return c.start < t; });
            for (; it != layer.clips.end() && it->start <= viewEnd; ++it)
            {
                if (it->end() < viewStart)
                {
                    continue;
                }
                const double x0 = row.getX() + geometry.xForTime(it->start);
                const double x1 = row.getX() + geometry.xForTime(it->end());
                const auto box =
                    juce::Rectangle<float>{static_cast<float>(std::max(x0, row.getX() - 4.0)),
                                           static_cast<float>(row.getY() + 3),
                                           static_cast<float>(std::min(x1, row.getRight() + 4.0) -
                                                              std::max(x0, row.getX() - 4.0)),
                                           static_cast<float>(row.getHeight() - 7)};
                g.setColour(colours.selection.withAlpha(0.55f));
                g.fillRoundedRectangle(box, theme.metrics.cornerRadius);
                g.setColour(colours.selection.brighter(0.4f));
                g.drawRoundedRectangle(box, theme.metrics.cornerRadius, 1.0f);
                const auto visible = box.getIntersection(row.toFloat());
                if (it->peaks)
                {
                    paintWaveform(g, visible.withTrimmedTop(18.0f).withTrimmedBottom(2.0f), *it,
                                  geometry, row.getX());
                }
                else if (visible.getWidth() > 60.0f)
                {
                    g.setColour(it->failed ? colours.error : colours.textSecondary);
                    g.setFont(juce::FontOptions{theme.fontSizes.small});
                    g.drawText(it->failed ? TRANS("Offline") : TRANS("Loading..."),
                               visible.withTrimmedTop(18.0f), juce::Justification::centred, true);
                }
                if (box.getWidth() > 24.0f)
                {
                    g.setColour(colours.textPrimary);
                    g.setFont(juce::FontOptions{theme.fontSizes.small});
                    g.drawText(it->name, box.reduced(5.0f, 2.0f).withHeight(15.0f),
                               juce::Justification::centredLeft, true);
                }
                ++drawn;
            }
        }

        const auto header = juce::Rectangle<int>{layout.headers.getX(), row.getY(),
                                                 layout.headers.getWidth(), row.getHeight()};
        if (header.getBottom() > layout.headers.getY() &&
            header.getY() < layout.headers.getBottom())
        {
            const juce::Graphics::ScopedSaveState state{g};
            g.reduceClipRegion(layout.headers);
            g.setColour(colours.panelHeader);
            g.fillRect(header.reduced(0, 1).withTrimmedRight(1));
            g.setColour(colours.textPrimary);
            g.setFont(juce::FontOptions{theme.fontSizes.normal});
            g.drawText(layer.name.isEmpty() ? TRANS("Layer") : layer.name,
                       header.reduced(10, 6).withHeight(20), juce::Justification::centredLeft,
                       true);
            g.setColour(colours.border);
            g.fillRect(header.withTop(header.getBottom() - 1));
        }
    }

    if (model.layers.empty())
    {
        g.setColour(colours.textSecondary);
        g.setFont(juce::FontOptions{theme.fontSizes.normal});
        g.drawText(TRANS("Import audio files (File > Import Audio...) or drop them here."),
                   layout.lanes, juce::Justification::centred, true);
    }

    if (playhead)
    {
        const juce::Graphics::ScopedSaveState state{g};
        g.reduceClipRegion(layout.ruler.getUnion(layout.lanes));
        paintPlayhead(g, layout, geometry, *playhead);
    }
    return drawn;
}

} // namespace af::ui
