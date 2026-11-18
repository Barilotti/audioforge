#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace af::ui
{

/// Horizontal stereo meter, -60...+6 dBFS: RMS bar, peak bar with 20 dB/s fall, peak hold for
/// 1.5 s, and a clip indicator that stays lit until clicked.
class LevelMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr float minDb = -60.0f;
    static constexpr float maxDb = 6.0f;
    static constexpr float fallDbPerSecond = 20.0f;
    static constexpr double holdSeconds = 1.5;

    LevelMeter();
    ~LevelMeter() override = default;

    /// Linear values (1.0 = 0 dBFS) measured since the previous call, `dtSeconds` ago.
    void setLevels(std::array<float, 2> peak, std::array<float, 2> rms, bool clipped,
                   double dtSeconds);

    /// Called when the user clicks the clip indicator.
    std::function<void()> onClipReset;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;

    [[nodiscard]] float displayedPeakDb(int channel) const noexcept;
    [[nodiscard]] float heldPeakDb(int channel) const noexcept;

private:
    [[nodiscard]] juce::Rectangle<int> clipArea() const;
    [[nodiscard]] float xForDb(float db, juce::Rectangle<float> bar) const noexcept;

    std::array<float, 2> peakDb_{minDb, minDb};
    std::array<float, 2> rmsDb_{minDb, minDb};
    std::array<float, 2> holdDb_{minDb, minDb};
    std::array<double, 2> holdAge_{};
    bool clipped_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};

} // namespace af::ui
