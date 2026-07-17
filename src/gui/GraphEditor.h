#pragma once

#include "core/GraphAudioProcessor.h"
#include "gui/ControlPanelComponent.h"
#include "gui/ModuleBrowserComponent.h"
#include "gui/NodeCanvasComponent.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace better
{
class GraphEditor final : public juce::AudioProcessorEditor,
                          private juce::ChangeListener,
                          private juce::Timer
{
public:
    explicit GraphEditor (GraphAudioProcessor& processor);
    ~GraphEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    void selectNode (GraphNodeUID uid);
    void syncSelectionPanel();
    void keepEndpointNodesVisible();
    juce::Point<float> getNextModulePosition();
    void chooseNAMFileForSelection();
    void chooseIRFileForSelection();

    GraphAudioProcessor& graphProcessor;
    ModuleBrowserComponent moduleBrowser;
    NodeCanvasComponent nodeCanvas;
    ControlPanelComponent controlPanel;
    GraphNodeUID selectedNode = inputNodeUID;
    int modulePlacementIndex = 0;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphEditor)
};
} // namespace better
