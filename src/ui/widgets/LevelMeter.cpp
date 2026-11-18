#include "ui/widgets/LevelMeter.h"

#include "ui/theme/Theme.h"

#include <algorithm>
#include <cmath>

namespace af::ui
{

namespace
{

constexpr int clipWidth = 18;
constexpr int scaleHeight = 12;

float toDb(float linear) noexcept
{
    return linear > 0.0f ? std::max(LevelMeter::minDb, 20.0f * std::log10(linear))
                         : LevelMeter::minDb;
}

} // namespace

LevelMeter::LevelMeter()
{
    setOpaque(false);
    setTooltip(TRANS("Master output: RMS (solid), peak (light) and peak hold. Click the red "
                     "indicator to reset it."));
}

void LevelMeter::setLevels(std::array<float, 2> peak, std::array<float, 2> rms, bool clipped,
                           double dtSeconds)
{
    const auto fall = fallDbPerSecond * static_cast<float>(dtSeconds);
    for (std::size_t ch = 0; ch < 2; ++ch)
    {
        const float db = toDb(peak[ch]);
        peakDb_[ch] = std::max(db, peakDb_[ch] - fall);
        rmsDb_[ch] = toDb(rms[ch]);
        holdAge_[ch] += dtSeconds;
        if (db >= holdDb_[ch])
        {
            holdDb_[ch] = db;
            holdAge_[ch] = 0.0;
        }
        else if (holdAge_[ch] > holdSeconds)
        {
            holdDb_[ch] = std::max(peakDb_[ch], holdDb_[ch] - fall);
        }
    }
    clipped_ = clipped;
    repaint();
}

float LevelMeter::displayedPeakDb(int channel) const noexcept
{
    return peakDb_[static_cast<std::size_t>(channel & 1)];
}

float LevelMeter::heldPeakDb(int channel) const noexcept
{
    return holdDb_[static_cast<std::size_t>(channel & 1)];
}

juce::Rectangle<int> LevelMeter::clipArea() const
{
    return getLocalBounds().removeFromRight(clipWidth).withTrimmedBottom(scaleHeight);
}

float LevelMeter::xForDb(float db, juce::Rectangle<float> bar) const noexcept
{
    const float t = (std::clamp(db, minDb, maxDb) - minDb) / (maxDb - minDb);
    return bar.getX() + t * bar.getWidth();
}

void LevelMeter::paint(juce::Graphics& g)
{
    const auto& colours = Theme::current().colours;
    auto area = getLocalBounds().toFloat();
    auto scale = area.removeFromBottom(static_cast<float>(scaleHeight));
    area.removeFromRight(static_cast<float>(clipWidth + 4));
    scale.removeFromRight(static_cast<float>(clipWidth + 4));

    g.setColour(colours.controlBackground);
    g.fillRoundedRectangle(area, 2.0f);

    const float rowGap = 2.0f;
    const float rowHeight = (area.getHeight() - 3.0f * rowGap) / 2.0f;
    const float zeroX = xForDb(0.0f, area);
    const float warnX = xForDb(-6.0f, area);
    for (std::size_t ch = 0; ch < 2; ++ch)
    {
        const auto row = juce::Rectangle<float>{area.getX() + 2.0f,
                                                area.getY() + rowGap +
                                                    static_cast<float>(ch) * (rowHeight + rowGap),
                                                area.getWidth() - 4.0f, rowHeight};
        auto paintBar = [&](float db, float alpha, juce::Rectangle<float> r)
        {
            const float x = xForDb(db, row);
            if (x <= r.getX())
            {
                return;
            }
            auto fill = [&](float from, float to, juce::Colour c)
            {
                const float a = std::max(from, r.getX());
                const float b = std::min(to, x);
                if (b > a)
                {
                    g.setColour(c.withMultipliedAlpha(alpha));
                    g.fillRect(juce::Rectangle<float>{a, r.getY(), b - a, r.getHeight()});
                }
            };
            fill(r.getX(), warnX, colours.meterNormal);
            fill(warnX, zeroX, colours.meterWarning);
            fill(zeroX, r.getRight(), colours.meterClip);
        };
        paintBar(peakDb_[ch], 0.45f, row);
        paintBar(rmsDb_[ch], 1.0f, row.reduced(0.0f, rowHeight * 0.2f));

        if (holdDb_[ch] > minDb)
        {
            const float x = xForDb(holdDb_[ch], row);
            g.setColour(holdDb_[ch] > 0.0f ? colours.meterClip : colours.textPrimary);
            g.fillRect(juce::Rectangle<float>{x - 1.0f, row.getY(), 2.0f, row.getHeight()});
        }
    }

    // Scale.
    g.setFont(juce::FontOptions{10.0f});
    for (const float db : {-48.0f, -36.0f, -24.0f, -18.0f, -12.0f, -6.0f, -3.0f, 0.0f, 6.0f})
    {
        const float x = xForDb(db, area);
        g.setColour(colours.border);
        g.drawVerticalLine(juce::roundToInt(x), area.getBottom() - 2.0f, area.getBottom());
        g.setColour(colours.textSecondary);
        g.drawText(juce::String(juce::roundToInt(db)),
                   juce::Rectangle<float>{x - 14.0f, scale.getY(), 28.0f, scale.getHeight()},
                   juce::Justification::centred, false);
    }

    // Clip indicator.
    const auto clip = clipArea().toFloat().reduced(1.0f);
    g.setColour(clipped_ ? colours.meterClip : colours.controlBackground);
    g.fillRoundedRectangle(clip, 2.0f);
    g.setColour(colours.controlOutline);
    g.drawRoundedRectangle(clip, 2.0f, 1.0f);
}

void LevelMeter::mouseDown(const juce::MouseEvent& event)
{
    if (clipArea().contains(event.getPosition()) && onClipReset)
    {
        onClipReset();
    }
}

} // namespace af::ui
