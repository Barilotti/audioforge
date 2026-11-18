#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace af::ui
{

/// Contents of the About dialog: product name, version, build and framework information.
class AboutComponent final : public juce::Component
{
public:
    AboutComponent();
    ~AboutComponent() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /// Opens the About dialog without blocking (modal loops are disabled in AudioForge).
    static void show(juce::Component* centreAround);

private:
    juce::TextButton closeButton_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AboutComponent)
};

} // namespace af::ui
