#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace af::ui
{

/// A titled panel of the main window. Holds one content component below its header.
class Panel final : public juce::Component
{
public:
    explicit Panel(juce::String title);
    ~Panel() override = default;

    void setContent(std::unique_ptr<juce::Component> content);
    [[nodiscard]] const juce::String& getTitle() const noexcept { return title_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::String title_;
    std::unique_ptr<juce::Component> content_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Panel)
};

/// Content shown in panels whose feature has not been built yet.
class PlaceholderContent final : public juce::Component
{
public:
    explicit PlaceholderContent(juce::String text);
    ~PlaceholderContent() override = default;

    void paint(juce::Graphics& g) override;

private:
    juce::String text_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaceholderContent)
};

} // namespace af::ui
