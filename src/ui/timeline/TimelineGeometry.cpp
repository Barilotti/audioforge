#include "ui/timeline/TimelineGeometry.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace af::ui
{

double TimelineGeometry::xForTime(double seconds) const noexcept
{
    return (seconds - viewStart_) / secondsPerPixel_;
}

double TimelineGeometry::timeForX(double x) const noexcept
{
    return viewStart_ + x * secondsPerPixel_;
}

double TimelineGeometry::scrollableSeconds() const noexcept
{
    return std::max(contentSeconds_ + endMarginSeconds, visibleSeconds());
}

void TimelineGeometry::setWidth(double pixels) noexcept
{
    width_ = std::max(pixels, 1.0);
    clamp();
}

void TimelineGeometry::setContentSeconds(double seconds) noexcept
{
    contentSeconds_ = std::max(seconds, 0.0);
    clamp();
}

void TimelineGeometry::setViewStart(double seconds) noexcept
{
    viewStart_ = seconds;
    clamp();
}

void TimelineGeometry::scrollByPixels(double pixels) noexcept
{
    setViewStart(viewStart_ + pixels * secondsPerPixel_);
}

void TimelineGeometry::zoomAround(double factor, double anchorX) noexcept
{
    const double anchorTime = timeForX(anchorX);
    secondsPerPixel_ =
        std::clamp(secondsPerPixel_ * factor, minSecondsPerPixel, maxSecondsPerPixel);
    viewStart_ = anchorTime - anchorX * secondsPerPixel_;
    clamp();
}

void TimelineGeometry::zoomToFit() noexcept
{
    const double seconds = std::max(contentSeconds_ + endMarginSeconds, 10.0);
    secondsPerPixel_ = std::clamp(seconds / width_, minSecondsPerPixel, maxSecondsPerPixel);
    viewStart_ = 0.0;
    clamp();
}

void TimelineGeometry::reveal(double seconds) noexcept
{
    if (seconds < viewStart_ || seconds > viewEnd())
    {
        setViewStart(seconds - 0.05 * visibleSeconds());
    }
}

double TimelineGeometry::rulerStep(double minPixels) const noexcept
{
    const double minSeconds = minPixels * secondsPerPixel_;
    if (minSeconds <= 5.0)
    {
        double scale = std::pow(10.0, std::floor(std::log10(std::max(minSeconds, 1e-9))));
        for (int i = 0; i < 4; ++i, scale *= 10.0)
        {
            for (const double m : {1.0, 2.0, 5.0})
            {
                if (m * scale >= minSeconds)
                {
                    return m * scale;
                }
            }
        }
    }
    constexpr std::array<double, 10> large{10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600};
    for (const double step : large)
    {
        if (step >= minSeconds)
        {
            return step;
        }
    }
    return std::ceil(minSeconds / 3600.0) * 3600.0;
}

void TimelineGeometry::clamp() noexcept
{
    const double maxStart = std::max(0.0, scrollableSeconds() - visibleSeconds());
    viewStart_ = std::clamp(viewStart_, 0.0, maxStart);
}

} // namespace af::ui
