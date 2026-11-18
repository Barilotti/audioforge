#pragma once

namespace af::ui
{

/// Horizontal mapping between timeline seconds and pixels of the lane area, with zoom and
/// scroll limits. Pure arithmetic, so it is tested without a window.
class TimelineGeometry
{
public:
    static constexpr double minSecondsPerPixel = 1.0 / 96'000.0; ///< About one sample per pixel.
    static constexpr double maxSecondsPerPixel = 60.0;
    static constexpr double endMarginSeconds = 2.0; ///< Room to scroll past the last clip.

    [[nodiscard]] double secondsPerPixel() const noexcept { return secondsPerPixel_; }
    [[nodiscard]] double viewStart() const noexcept { return viewStart_; }
    [[nodiscard]] double width() const noexcept { return width_; }
    [[nodiscard]] double contentSeconds() const noexcept { return contentSeconds_; }

    [[nodiscard]] double xForTime(double seconds) const noexcept;
    [[nodiscard]] double timeForX(double x) const noexcept;
    [[nodiscard]] double visibleSeconds() const noexcept { return width_ * secondsPerPixel_; }
    [[nodiscard]] double viewEnd() const noexcept { return viewStart_ + visibleSeconds(); }
    /// Seconds the horizontal scroll bar spans: the content plus a margin, at least the view.
    [[nodiscard]] double scrollableSeconds() const noexcept;

    void setWidth(double pixels) noexcept;
    void setContentSeconds(double seconds) noexcept;
    void setViewStart(double seconds) noexcept;
    void scrollByPixels(double pixels) noexcept;
    /// Multiplies seconds per pixel by `factor` (< 1 zooms in) keeping the time under `anchorX`.
    void zoomAround(double factor, double anchorX) noexcept;
    /// Shows the whole content (at least 10 seconds).
    void zoomToFit() noexcept;
    /// Scrolls so that `seconds` is visible, a page at a time (playhead follow).
    void reveal(double seconds) noexcept;

    /// A "nice" ruler step in seconds (1/2/5 x 10^n, then 10 s, 15 s, 30 s, minutes) that is at
    /// least `minPixels` wide at this zoom.
    [[nodiscard]] double rulerStep(double minPixels = 80.0) const noexcept;

private:
    void clamp() noexcept;

    double secondsPerPixel_ = 0.01;
    double viewStart_ = 0.0;
    double width_ = 1.0;
    double contentSeconds_ = 0.0;
};

} // namespace af::ui
