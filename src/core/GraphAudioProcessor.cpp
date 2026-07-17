#include "core/GraphAudioProcessor.h"

#include "audio/graph/ModuleRegistry.h"
#include "gui/GraphEditor.h"

#include <algorithm>
#include <cmath>

namespace better
{
namespace
{
constexpr auto stateTag = "BetterArchetypeGraphState";
constexpr auto nodesTag = "Nodes";
constexpr auto nodeTag = "Node";
constexpr auto connectionsTag = "Connections";
constexpr auto connectionTag = "Connection";

float applySafetyLimiter (float sample) noexcept
{
    sample = sanitiseAudioSample (sample);

    constexpr auto threshold = 0.98f;

    if (sample > threshold)
        return threshold + (std::tanh ((sample - threshold) * 0.3f) * (1.0f - threshold));

    if (sample < -threshold)
        return -threshold + (std::tanh ((sample + threshold) * 0.3f) * (1.0f - threshold));

    return sample;
}
} // namespace

GraphAudioProcessor::GraphAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    audioGraph.setPlayConfigDetails (2, 2, currentSampleRate, currentBlockSize);
    buildDefaultGraph();
}

GraphAudioProcessor::~GraphAudioProcessor() = default;

void GraphAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = juce::jmax (1, samplesPerBlock);

    const juce::ScopedLock lock (graphLock);
    audioGraph.setPlayConfigDetails (getTotalNumInputChannels(),
                                     getTotalNumOutputChannels(),
                                     currentSampleRate,
                                     currentBlockSize);
    audioGraph.prepareToPlay (currentSampleRate, currentBlockSize);
    rebuildAudioGraphConnections();
}

void GraphAudioProcessor::releaseResources()
{
    audioGraph.releaseResources();
}

void GraphAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    audioGraph.processBlock (buffer, midiMessages);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            samples[sample] = applySafetyLimiter (samples[sample]);
    }
}

juce::AudioProcessorEditor* GraphAudioProcessor::createEditor()
{
    return new GraphEditor (*this);
}

void GraphAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused (index);
}

const juce::String GraphAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void GraphAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

bool GraphAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();

    if (input.isDisabled() || output.isDisabled())
        return false;

    return (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo())
        && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

bool GraphAudioProcessor::addModuleNode (const juce::String& moduleId,
                                         juce::Point<float> position,
                                         GraphNodeUID& createdNode)
{
    const juce::ScopedLock lock (graphLock);

    const auto uid = nextModuleNode++;

    if (! addModuleNodeInternal (uid, moduleId, position, nullptr))
        return false;

    createdNode = uid;
    audioGraph.rebuild();
    sendChangeMessage();
    return true;
}

bool GraphAudioProcessor::removeGraphNode (GraphNodeUID uid)
{
    if (uid == inputNodeUID || uid == outputNodeUID)
        return false;

    const juce::ScopedLock lock (graphLock);

    if (nodeRecords.find (uid) == nodeRecords.end())
        return false;

    audioGraph.removeNode (toNodeID (uid), Graph::UpdateKind::none);
    nodeRecords.erase (uid);

    logicalConnections.erase (std::remove_if (logicalConnections.begin(), logicalConnections.end(), [uid] (const auto& connection)
    {
        return connection.source == uid || connection.destination == uid;
    }), logicalConnections.end());

    rebuildAudioGraphConnections();
    sendChangeMessage();
    return true;
}

bool GraphAudioProcessor::addGraphConnection (GraphNodeUID source, GraphNodeUID destination)
{
    const juce::ScopedLock lock (graphLock);

    if (! addLogicalConnectionInternal ({ source, destination }))
        return false;

    rebuildAudioGraphConnections();
    sendChangeMessage();
    return true;
}

bool GraphAudioProcessor::removeGraphConnection (GraphConnectionDescription connection)
{
    const juce::ScopedLock lock (graphLock);

    const auto oldSize = logicalConnections.size();
    logicalConnections.erase (std::remove (logicalConnections.begin(), logicalConnections.end(), connection),
                              logicalConnections.end());

    if (oldSize == logicalConnections.size())
        return false;

    rebuildAudioGraphConnections();
    sendChangeMessage();
    return true;
}

void GraphAudioProcessor::setGraphNodePosition (GraphNodeUID uid, juce::Point<float> position)
{
    const juce::ScopedLock lock (graphLock);

    if (auto it = nodeRecords.find (uid); it != nodeRecords.end())
    {
        it->second.description.x = position.x;
        it->second.description.y = position.y;
    }
}

std::vector<GraphNodeDescription> GraphAudioProcessor::getGraphNodes() const
{
    const juce::ScopedLock lock (graphLock);

    std::vector<GraphNodeDescription> result;
    result.reserve (nodeRecords.size());

    for (const auto& [uid, record] : nodeRecords)
    {
        juce::ignoreUnused (uid);
        result.push_back (record.description);
    }

    return result;
}

std::vector<GraphConnectionDescription> GraphAudioProcessor::getGraphConnections() const
{
    const juce::ScopedLock lock (graphLock);
    return logicalConnections;
}

GraphNodeDescription GraphAudioProcessor::getGraphNode (GraphNodeUID uid) const
{
    const juce::ScopedLock lock (graphLock);

    if (auto it = nodeRecords.find (uid); it != nodeRecords.end())
        return it->second.description;

    return {};
}

AudioModuleProcessor* GraphAudioProcessor::getModuleProcessorForNode (GraphNodeUID uid) const
{
    const juce::ScopedLock lock (graphLock);

    if (auto* node = audioGraph.getNodeForId (toNodeID (uid)))
        return dynamic_cast<AudioModuleProcessor*> (node->getProcessor());

    return nullptr;
}

void GraphAudioProcessor::loadNAMFileForNode (GraphNodeUID uid, const juce::File& file)
{
    if (auto* processor = getModuleProcessorForNode (uid))
        processor->loadNAMFileAsync (file);
}

void GraphAudioProcessor::loadIRFileForNode (GraphNodeUID uid, const juce::File& file)
{
    if (auto* processor = getModuleProcessorForNode (uid))
        processor->loadIRFileAsync (file);
}

void GraphAudioProcessor::buildDefaultGraph()
{
    const juce::ScopedLock lock (graphLock);

    audioGraph.clear (Graph::UpdateKind::none);
    nodeRecords.clear();
    logicalConnections.clear();
    nextModuleNode = firstModuleNodeUID;

    addIONode (inputNodeUID, GraphNodeRole::input, { 130.0f, 260.0f });
    addIONode (outputNodeUID, GraphNodeRole::output, { 820.0f, 260.0f });
    addLogicalConnectionInternal ({ inputNodeUID, outputNodeUID });
    rebuildAudioGraphConnections();
}

bool GraphAudioProcessor::addIONode (GraphNodeUID uid, GraphNodeRole role, juce::Point<float> position)
{
    const auto type = role == GraphNodeRole::input
                    ? Graph::AudioGraphIOProcessor::audioInputNode
                    : Graph::AudioGraphIOProcessor::audioOutputNode;

    auto node = audioGraph.addNode (std::make_unique<Graph::AudioGraphIOProcessor> (type),
                                    toNodeID (uid),
                                    Graph::UpdateKind::none);

    if (node == nullptr)
        return false;

    NodeRecord record;
    record.description.uid = uid;
    record.description.role = role;
    record.description.name = role == GraphNodeRole::input ? "Input" : "Output";
    record.description.x = position.x;
    record.description.y = position.y;
    nodeRecords[uid] = record;
    return true;
}

bool GraphAudioProcessor::addModuleNodeInternal (GraphNodeUID uid,
                                                 const juce::String& moduleId,
                                                 juce::Point<float> position,
                                                 const juce::ValueTree* moduleState)
{
    auto processor = ModuleRegistry::createProcessor (moduleId);

    if (processor == nullptr)
        return false;

    const auto moduleName = processor->getDescriptor().name;

    if (moduleState != nullptr)
        processor->restoreModuleState (*moduleState);

    auto node = audioGraph.addNode (std::move (processor), toNodeID (uid), Graph::UpdateKind::none);

    if (node == nullptr)
        return false;

    NodeRecord record;
    record.description.uid = uid;
    record.description.role = GraphNodeRole::module;
    record.description.moduleId = moduleId;
    record.description.name = moduleName;
    record.description.x = position.x;
    record.description.y = position.y;
    nodeRecords[uid] = record;
    nextModuleNode = juce::jmax (nextModuleNode, uid + 1);
    return true;
}

bool GraphAudioProcessor::canAddLogicalConnection (GraphConnectionDescription connection) const
{
    if (connection.source == 0 || connection.destination == 0 || connection.source == connection.destination)
        return false;

    if (std::find (logicalConnections.begin(), logicalConnections.end(), connection) != logicalConnections.end())
        return false;

    const auto source = nodeRecords.find (connection.source);
    const auto destination = nodeRecords.find (connection.destination);

    if (source == nodeRecords.end() || destination == nodeRecords.end())
        return false;

    if (source->second.description.role == GraphNodeRole::output
        || destination->second.description.role == GraphNodeRole::input)
    {
        return false;
    }

    return ! audioGraph.isAnInputTo (toNodeID (connection.destination), toNodeID (connection.source));
}

bool GraphAudioProcessor::addLogicalConnectionInternal (GraphConnectionDescription connection)
{
    if (! canAddLogicalConnection (connection))
        return false;

    logicalConnections.push_back (connection);
    return true;
}

void GraphAudioProcessor::rebuildAudioGraphConnections()
{
    const auto existingConnections = audioGraph.getConnections();

    for (const auto& connection : existingConnections)
        audioGraph.removeConnection (connection, Graph::UpdateKind::none);

    std::vector<GraphConnectionDescription> keptConnections;
    keptConnections.reserve (logicalConnections.size());

    for (const auto& connection : logicalConnections)
    {
        if (addAudioConnectionsFor (connection))
            keptConnections.push_back (connection);
    }

    logicalConnections = std::move (keptConnections);
    audioGraph.rebuild();
}

bool GraphAudioProcessor::addAudioConnectionsFor (GraphConnectionDescription connection)
{
    const auto sourceChannels = getNodeOutputChannelCount (connection.source);
    const auto destinationChannels = getNodeInputChannelCount (connection.destination);

    if (sourceChannels <= 0 || destinationChannels <= 0)
        return false;

    auto addedAny = false;

    // Eine logische Node-Verbindung wird auf echte JUCE-Kanalverbindungen abgebildet.
    // Mono-Quellen werden dabei bei Bedarf auf Stereo-Ziele gespiegelt.
    for (int destinationChannel = 0; destinationChannel < destinationChannels; ++destinationChannel)
    {
        const auto sourceChannel = juce::jmin (destinationChannel, sourceChannels - 1);

        const Graph::Connection graphConnection
        {
            { toNodeID (connection.source), sourceChannel },
            { toNodeID (connection.destination), destinationChannel }
        };

        if (audioGraph.addConnection (graphConnection, Graph::UpdateKind::none))
            addedAny = true;
    }

    return addedAny;
}

int GraphAudioProcessor::getNodeOutputChannelCount (GraphNodeUID uid) const
{
    if (auto* node = audioGraph.getNodeForId (toNodeID (uid)))
        return node->getProcessor()->getTotalNumOutputChannels();

    return 0;
}

int GraphAudioProcessor::getNodeInputChannelCount (GraphNodeUID uid) const
{
    if (auto* node = audioGraph.getNodeForId (toNodeID (uid)))
        return node->getProcessor()->getTotalNumInputChannels();

    return 0;
}

juce::String GraphAudioProcessor::roleToString (GraphNodeRole role)
{
    switch (role)
    {
        case GraphNodeRole::input:  return "input";
        case GraphNodeRole::output: return "output";
        case GraphNodeRole::module: return "module";
    }

    return "module";
}

GraphNodeRole GraphAudioProcessor::stringToRole (const juce::String& role)
{
    if (role == "input")  return GraphNodeRole::input;
    if (role == "output") return GraphNodeRole::output;
    return GraphNodeRole::module;
}

void GraphAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const juce::ScopedLock lock (graphLock);

    juce::ValueTree state (stateTag);
    state.setProperty ("version", 1, nullptr);
    state.setProperty ("nextModuleNode", (int) nextModuleNode, nullptr);

    juce::ValueTree nodes (nodesTag);

    for (const auto& [uid, record] : nodeRecords)
    {
        juce::ValueTree nodeState (nodeTag);
        nodeState.setProperty ("uid", (int) uid, nullptr);
        nodeState.setProperty ("role", roleToString (record.description.role), nullptr);
        nodeState.setProperty ("moduleId", record.description.moduleId, nullptr);
        nodeState.setProperty ("x", record.description.x, nullptr);
        nodeState.setProperty ("y", record.description.y, nullptr);

        if (auto* processor = dynamic_cast<AudioModuleProcessor*> (audioGraph.getNodeForId (toNodeID (uid))->getProcessor()))
            nodeState.addChild (processor->createModuleState(), -1, nullptr);

        nodes.addChild (nodeState, -1, nullptr);
    }

    juce::ValueTree connections (connectionsTag);

    for (const auto& connection : logicalConnections)
    {
        juce::ValueTree connectionState (connectionTag);
        connectionState.setProperty ("source", (int) connection.source, nullptr);
        connectionState.setProperty ("destination", (int) connection.destination, nullptr);
        connections.addChild (connectionState, -1, nullptr);
    }

    state.addChild (nodes, -1, nullptr);
    state.addChild (connections, -1, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void GraphAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    if (! state.hasType (stateTag))
        return;

    suspendProcessing (true);

    {
        const juce::ScopedLock lock (graphLock);

        audioGraph.clear (Graph::UpdateKind::none);
        nodeRecords.clear();
        logicalConnections.clear();
        nextModuleNode = (GraphNodeUID) juce::jmax (firstModuleNodeUID, (GraphNodeUID) (int) state.getProperty ("nextModuleNode", (int) firstModuleNodeUID));

        addIONode (inputNodeUID, GraphNodeRole::input, { 130.0f, 260.0f });
        addIONode (outputNodeUID, GraphNodeRole::output, { 820.0f, 260.0f });

        if (const auto nodes = state.getChildWithName (nodesTag); nodes.isValid())
        {
            for (int i = 0; i < nodes.getNumChildren(); ++i)
            {
                const auto nodeState = nodes.getChild (i);
                const auto uid = (GraphNodeUID) (int) nodeState.getProperty ("uid", 0);
                const auto role = stringToRole (nodeState.getProperty ("role").toString());
                const juce::Point<float> position
                {
                    (float) nodeState.getProperty ("x", 100.0f),
                    (float) nodeState.getProperty ("y", 100.0f)
                };

                if (uid == inputNodeUID || uid == outputNodeUID)
                {
                    if (auto it = nodeRecords.find (uid); it != nodeRecords.end())
                    {
                        it->second.description.x = position.x;
                        it->second.description.y = position.y;
                    }

                    continue;
                }

                if (role == GraphNodeRole::module)
                {
                    const auto moduleState = nodeState.getChildWithName ("ModuleState");
                    const auto moduleId = nodeState.getProperty ("moduleId").toString();
                    addModuleNodeInternal (uid, moduleId, position, moduleState.isValid() ? &moduleState : nullptr);
                }
            }
        }

        if (const auto connections = state.getChildWithName (connectionsTag); connections.isValid())
        {
            for (int i = 0; i < connections.getNumChildren(); ++i)
            {
                const auto connectionState = connections.getChild (i);
                addLogicalConnectionInternal ({
                    (GraphNodeUID) (int) connectionState.getProperty ("source", 0),
                    (GraphNodeUID) (int) connectionState.getProperty ("destination", 0)
                });
            }
        }

        if (logicalConnections.empty())
            addLogicalConnectionInternal ({ inputNodeUID, outputNodeUID });

        audioGraph.setPlayConfigDetails (getTotalNumInputChannels(),
                                         getTotalNumOutputChannels(),
                                         currentSampleRate,
                                         currentBlockSize);
        audioGraph.prepareToPlay (currentSampleRate, currentBlockSize);
        rebuildAudioGraphConnections();
    }

    suspendProcessing (false);
    sendChangeMessage();
}
} // namespace better
