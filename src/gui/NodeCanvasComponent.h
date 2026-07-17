#pragma once

#include "audio/graph/GraphTypes.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <optional>

namespace better
{
class NodeCanvasComponent final : public juce::Component
{
public:
    NodeCanvasComponent();

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDrag (const juce::MouseEvent& event) override;
    void mouseUp (const juce::MouseEvent& event) override;
    bool keyPressed (const juce::KeyPress& key) override;

    void setSelectedNode (GraphNodeUID uid);

    std::function<std::vector<GraphNodeDescription>()> getNodes;
    std::function<std::vector<GraphConnectionDescription>()> getConnections;
    std::function<void (GraphNodeUID uid)> onNodeSelected;
    std::function<void (GraphNodeUID uid, juce::Point<float> position)> onNodeMoved;
    std::function<void (GraphNodeUID source, GraphNodeUID destination)> onConnectionRequested;
    std::function<void (GraphConnectionDescription connection)> onConnectionRemoveRequested;
    std::function<void (GraphNodeUID uid)> onNodeDeleteRequested;

private:
    struct PinHit
    {
        GraphNodeUID uid = 0;
        bool isOutput = false;
    };

    juce::Rectangle<float> getNodeBounds (const GraphNodeDescription& node) const;
    juce::Point<float> getInputPin (const GraphNodeDescription& node) const;
    juce::Point<float> getOutputPin (const GraphNodeDescription& node) const;
    std::optional<GraphNodeDescription> findNodeAt (juce::Point<float> position) const;
    std::optional<PinHit> findPinAt (juce::Point<float> position) const;
    std::optional<GraphConnectionDescription> findConnectionAt (juce::Point<float> position) const;
    static float distanceToLine (juce::Point<float> point, juce::Point<float> a, juce::Point<float> b);
    static bool hasInputPin (const GraphNodeDescription& node);
    static bool hasOutputPin (const GraphNodeDescription& node);
    void drawNode (juce::Graphics& g, const GraphNodeDescription& node);

    GraphNodeUID selectedNode = inputNodeUID;
    GraphNodeUID pendingConnectionSource = 0;
    GraphNodeUID draggingNode = 0;
    juce::Point<float> dragOffset;
    juce::Point<float> lastMousePosition;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NodeCanvasComponent)
};
} // namespace better
