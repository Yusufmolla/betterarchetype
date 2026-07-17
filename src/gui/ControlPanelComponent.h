#pragma once

#include "audio/graph/GraphTypes.h"
#include "audio/modules/ModuleProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace better
{
class ControlPanelComponent final : public juce::Component
{
public:
    ControlPanelComponent();

    void paint (juce::Graphics& g) override;
    void resized() override;

    void setSelection (GraphNodeDescription node, AudioModuleProcessor* processor);
    void setStatusText (const juce::String& text);

    std::function<void()> onDeleteRequested;
    std::function<void()> onLoadNAMRequested;
    std::function<void()> onLoadIRRequested;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void clearControls();
    void addControlsFor (AudioModuleProcessor& processor);

    GraphNodeDescription selectedNode;
    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::TextButton deleteButton { "Delete" };
    juce::TextButton loadNAMButton { "Load NAM" };
    juce::TextButton loadIRButton { "Load IR" };

    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::ToggleButton> toggles;
    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ControlPanelComponent)
};
} // namespace better
