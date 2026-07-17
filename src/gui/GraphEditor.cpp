#include "gui/GraphEditor.h"

namespace better
{
namespace
{
constexpr auto graphNodeWidth = 132.0f;
constexpr auto graphNodeHeight = 64.0f;
constexpr auto endpointMargin = 24.0f;
const auto backgroundColour = juce::Colour (0xff0c0e12);
} // namespace

GraphEditor::GraphEditor (GraphAudioProcessor& processorIn)
    : AudioProcessorEditor (&processorIn),
      graphProcessor (processorIn)
{
    setSize (1320, 820);
    setResizable (true, true);
    setResizeLimits (980, 620, 1900, 1200);

    addAndMakeVisible (moduleBrowser);
    addAndMakeVisible (nodeCanvas);
    addAndMakeVisible (controlPanel);

    moduleBrowser.onModuleRequested = [this] (juce::String moduleId)
    {
        GraphNodeUID uid = 0;

        if (graphProcessor.addModuleNode (moduleId, getNextModulePosition(), uid))
            selectNode (uid);
    };

    nodeCanvas.getNodes = [this] { return graphProcessor.getGraphNodes(); };
    nodeCanvas.getConnections = [this] { return graphProcessor.getGraphConnections(); };
    nodeCanvas.onNodeSelected = [this] (GraphNodeUID uid) { selectNode (uid); };
    nodeCanvas.onNodeMoved = [this] (GraphNodeUID uid, juce::Point<float> position)
    {
        graphProcessor.setGraphNodePosition (uid, position);
    };
    nodeCanvas.onConnectionRequested = [this] (GraphNodeUID source, GraphNodeUID destination)
    {
        graphProcessor.addGraphConnection (source, destination);
        nodeCanvas.repaint();
    };
    nodeCanvas.onConnectionRemoveRequested = [this] (GraphConnectionDescription connection)
    {
        graphProcessor.removeGraphConnection (connection);
        nodeCanvas.repaint();
    };
    nodeCanvas.onNodeDeleteRequested = [this] (GraphNodeUID uid)
    {
        if (graphProcessor.removeGraphNode (uid))
            selectNode (inputNodeUID);
    };

    controlPanel.onDeleteRequested = [this]
    {
        if (graphProcessor.removeGraphNode (selectedNode))
            selectNode (inputNodeUID);
    };
    controlPanel.onLoadNAMRequested = [this] { chooseNAMFileForSelection(); };
    controlPanel.onLoadIRRequested = [this] { chooseIRFileForSelection(); };

    graphProcessor.addChangeListener (this);
    selectNode (inputNodeUID);
    startTimerHz (8);
}

GraphEditor::~GraphEditor()
{
    stopTimer();
    graphProcessor.removeChangeListener (this);
}

void GraphEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);
}

void GraphEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto browserArea = area.removeFromLeft (230);
    area.removeFromLeft (10);
    auto controlsArea = area.removeFromRight (310);
    area.removeFromRight (10);

    moduleBrowser.setBounds (browserArea);
    nodeCanvas.setBounds (area);
    controlPanel.setBounds (controlsArea);

    keepEndpointNodesVisible();
    nodeCanvas.repaint();
}

void GraphEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    juce::ignoreUnused (source);

    if (graphProcessor.getGraphNode (selectedNode).uid == 0)
        selectedNode = inputNodeUID;

    nodeCanvas.setSelectedNode (selectedNode);
    syncSelectionPanel();
    nodeCanvas.repaint();
}

void GraphEditor::timerCallback()
{
    if (auto* moduleProcessor = graphProcessor.getModuleProcessorForNode (selectedNode))
        controlPanel.setStatusText (moduleProcessor->getStatusText());
}

void GraphEditor::selectNode (GraphNodeUID uid)
{
    selectedNode = uid;
    nodeCanvas.setSelectedNode (uid);
    syncSelectionPanel();
}

void GraphEditor::syncSelectionPanel()
{
    const auto node = graphProcessor.getGraphNode (selectedNode);
    controlPanel.setSelection (node, graphProcessor.getModuleProcessorForNode (selectedNode));
}

void GraphEditor::keepEndpointNodesVisible()
{
    const auto canvasBounds = nodeCanvas.getLocalBounds().toFloat();

    if (canvasBounds.isEmpty())
        return;

    const auto maxY = juce::jmax (endpointMargin, canvasBounds.getHeight() - graphNodeHeight - endpointMargin);
    const auto y = juce::jlimit (endpointMargin, maxY, (canvasBounds.getHeight() - graphNodeHeight) * 0.5f);
    const auto outputX = juce::jmax (endpointMargin, canvasBounds.getWidth() - graphNodeWidth - endpointMargin);

    graphProcessor.setGraphNodePosition (inputNodeUID, { endpointMargin, y });
    graphProcessor.setGraphNodePosition (outputNodeUID, { outputX, y });
}

juce::Point<float> GraphEditor::getNextModulePosition()
{
    const auto local = nodeCanvas.getLocalBounds().toFloat();
    const auto baseX = juce::jlimit (80.0f, juce::jmax (80.0f, local.getWidth() - 220.0f), 260.0f + (float) ((modulePlacementIndex % 3) * 160));
    const auto baseY = juce::jlimit (70.0f, juce::jmax (70.0f, local.getHeight() - 140.0f), 150.0f + (float) ((modulePlacementIndex / 3) * 92));
    ++modulePlacementIndex;
    return { baseX, baseY };
}

void GraphEditor::chooseNAMFileForSelection()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load NAM model", juce::File {}, "*.nam");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (file.existsAsFile())
            graphProcessor.loadNAMFileForNode (selectedNode, file);
    });
}

void GraphEditor::chooseIRFileForSelection()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load IR", juce::File {}, "*.wav;*.aif;*.aiff;*.flac");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& chooser)
    {
        const auto file = chooser.getResult();

        if (file.existsAsFile())
            graphProcessor.loadIRFileForNode (selectedNode, file);
    });
}
} // namespace better
