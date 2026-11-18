#include "ui/timeline/TimelineView.h"

#include "ui/theme/Theme.h"

#include <cmath>

namespace af::ui
{

namespace
{
constexpr int scrollBarThickness = 12;
} // namespace

TimelineView::TimelineView(app::DeviceService& devices, app::Playback& playback)
    : devices_(devices)
    , playback_(playback)
    , vblank_(this, [this] { refresh(); })
{
    for (auto* bar : {&horizontal_, &vertical_})
    {
        bar->setAutoHide(false);
        bar->addListener(this);
        addAndMakeVisible(*bar);
    }
    setOpaque(true);
    refresh();
}

TimelineView::~TimelineView()
{
    horizontal_.removeListener(this);
    vertical_.removeListener(this);
}

void TimelineView::resized()
{
    auto area = getLocalBounds();
    vertical_.setBounds(area.removeFromRight(scrollBarThickness)
                            .withTrimmedTop(TimelineLayout::rulerHeight)
                            .withTrimmedBottom(scrollBarThickness));
    horizontal_.setBounds(
        area.removeFromBottom(scrollBarThickness).withTrimmedLeft(TimelineLayout::headerWidth));
    const auto scroll = layout_.verticalScroll;
    layout_ = TimelineLayout::forArea(area);
    layout_.verticalScroll = scroll;
    geometry_.setWidth(layout_.lanes.getWidth());
    if (!fitted_ && playback_.document() != nullptr)
    {
        geometry_.zoomToFit();
        fitted_ = true;
    }
    updateScrollBars();
}

void TimelineView::paint(juce::Graphics& g)
{
    g.fillAll(Theme::current().colours.panelBackground);
    TimelinePainter::paint(g, layout_, geometry_, model_, shownPlayhead_);
}

std::optional<double> TimelineView::playheadSeconds() const
{
    const auto spec = devices_.renderSpec();
    if (!spec || playback_.document() == nullptr || playback_.status().testTone)
    {
        return std::nullopt;
    }
    return static_cast<double>(playback_.transport().position()) /
           static_cast<double>(spec->sampleRate);
}

juce::Rectangle<int> TimelineView::playheadStrip(double seconds) const
{
    const auto x = juce::roundToInt(layout_.lanes.getX() + geometry_.xForTime(seconds));
    return {x - 8, layout_.ruler.getY(), 16, layout_.lanes.getBottom() - layout_.ruler.getY()};
}

void TimelineView::refresh()
{
    if (playback_.revision() != revision_)
    {
        revision_ = playback_.revision();
        const auto* document = playback_.document();
        model_ = document != nullptr
                     ? TimelineModel::fromProject(document->project, document->event)
                     : TimelineModel{};
        geometry_.setContentSeconds(model_.lengthSeconds);
        geometry_.zoomToFit();
        fitted_ = document != nullptr;
        layout_.verticalScroll = 0;
        shownPlayhead_.reset();
        updateScrollBars();
        repaint();
    }

    const auto playhead = playheadSeconds();
    if (playhead && playback_.transport().isPlaying())
    {
        const double before = geometry_.viewStart();
        geometry_.reveal(*playhead);
        if (geometry_.viewStart() != before)
        {
            shownPlayhead_ = playhead;
            updateScrollBars();
            repaint();
            return;
        }
    }
    if (playhead != shownPlayhead_)
    {
        if (shownPlayhead_)
        {
            repaint(playheadStrip(*shownPlayhead_));
        }
        if (playhead)
        {
            repaint(playheadStrip(*playhead));
        }
        shownPlayhead_ = playhead;
    }
}

void TimelineView::updateScrollBars()
{
    horizontal_.setRangeLimits(0.0, geometry_.scrollableSeconds(), juce::dontSendNotification);
    horizontal_.setCurrentRange(geometry_.viewStart(), geometry_.visibleSeconds(),
                                juce::dontSendNotification);
    const auto content =
        std::max(layout_.contentHeight(model_.layers.size()), layout_.lanes.getHeight());
    layout_.verticalScroll =
        std::clamp(layout_.verticalScroll, 0, content - layout_.lanes.getHeight());
    vertical_.setRangeLimits(0.0, content, juce::dontSendNotification);
    vertical_.setCurrentRange(layout_.verticalScroll, layout_.lanes.getHeight(),
                              juce::dontSendNotification);
}

void TimelineView::scrollBarMoved(juce::ScrollBar* bar, double newRangeStart)
{
    if (bar == &horizontal_)
    {
        geometry_.setViewStart(newRangeStart);
    }
    else
    {
        layout_.verticalScroll = juce::roundToInt(newRangeStart);
    }
    updateScrollBars();
    repaint();
}

void TimelineView::seekToX(int x)
{
    const auto spec = devices_.renderSpec();
    if (!spec || playback_.document() == nullptr)
    {
        return;
    }
    const double seconds =
        std::clamp(geometry_.timeForX(x - layout_.lanes.getX()), 0.0, geometry_.contentSeconds());
    playback_.transport().seek(static_cast<core::FrameCount>(
        std::llround(seconds * static_cast<double>(spec->sampleRate))));
}

void TimelineView::mouseDown(const juce::MouseEvent& event)
{
    if (layout_.ruler.contains(event.getPosition()))
    {
        seekToX(event.x);
    }
}

void TimelineView::mouseDrag(const juce::MouseEvent& event)
{
    if (layout_.ruler.contains(event.getMouseDownPosition()))
    {
        seekToX(event.x);
    }
}

void TimelineView::mouseDoubleClick(const juce::MouseEvent& event)
{
    if (layout_.ruler.contains(event.getPosition()))
    {
        geometry_.zoomToFit();
        updateScrollBars();
        repaint();
    }
}

void TimelineView::mouseWheelMove(const juce::MouseEvent& event,
                                  const juce::MouseWheelDetails& wheel)
{
    const auto mods = event.mods;
    if (mods.isCommandDown() || mods.isCtrlDown())
    {
        const double factor = std::exp(-static_cast<double>(wheel.deltaY) * 2.5);
        geometry_.zoomAround(factor, event.x - layout_.lanes.getX());
    }
    else if (mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY))
    {
        const float delta = mods.isShiftDown() ? wheel.deltaY : wheel.deltaX;
        geometry_.scrollByPixels(-static_cast<double>(delta) * layout_.lanes.getWidth() * 0.5);
    }
    else
    {
        layout_.verticalScroll -=
            juce::roundToInt(wheel.deltaY * TimelineLayout::layerHeight * 3.0f);
    }
    updateScrollBars();
    repaint();
}

} // namespace af::ui
