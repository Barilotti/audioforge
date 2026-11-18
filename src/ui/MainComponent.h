#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace af::ui
{

/// Root component of the main window. Stage S0.1 placeholder; the panel layout arrives in S0.4.
class MainComponent final : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override = default;

    void paint(juce::Graphics& g) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace af::ui
