#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace better
{
class ModuleBrowserComponent final : public juce::Component
{
public:
    ModuleBrowserComponent();

    void paint (juce::Graphics& g) override;
    void resized() override;

    std::function<void (juce::String moduleId)> onModuleRequested;

private:
    void rebuildButtons();

    juce::Label titleLabel;
    juce::OwnedArray<juce::TextButton> moduleButtons;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModuleBrowserComponent)
};
} // namespace better
