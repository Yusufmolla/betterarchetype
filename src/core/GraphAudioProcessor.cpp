#include "core/GraphAudioProcessor.h"

#include "audio/graph/ModuleRegistry.h"
#include "gui/GraphEditor.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace better
{
namespace
{
constexpr auto stateTag = "BetterArchetypeGraphState";
constexpr auto nodesTag = "Nodes";
constexpr auto nodeTag = "Node";
constexpr auto connectionsTag = "Connections";
constexpr auto connectionTag = "Connection";

// Parse and validate host state before any runtime graph is touched.
struct ParsedGraphState
{
    GraphDocument document = GraphDocument::createEmpty();
    std::map<GraphNodeUID, juce::ValueTree> moduleStates;
};

template <typename Number>
bool parseNumber (const juce::String& text, Number& result)
{
    const auto utf8 = text.toStdString();
    const auto* begin = utf8.data();
    const auto* end = begin + utf8.size();
    const auto parsed = std::from_chars (begin, end, result);
    return parsed.ec == std::errc {} && parsed.ptr == end;
}

bool readIntegerProperty (const juce::ValueTree& tree, const juce::Identifier& property, juce::int64& result)
{
    if (! tree.hasProperty (property))
        return false;

    const auto value = tree.getProperty (property);

    if (value.isInt() || value.isInt64())
    {
        result = static_cast<juce::int64> (value);
        return true;
    }

    return value.isString() && parseNumber (value.toString(), result);
}

bool readNodeUID (const juce::ValueTree& tree, const juce::Identifier& property, GraphNodeUID& result)
{
    juce::int64 value = 0;

    if (! readIntegerProperty (tree, property, value)
        || value <= 0
        || value > std::numeric_limits<int>::max())
    {
        return false;
    }

    result = static_cast<GraphNodeUID> (value);
    return true;
}

bool readPosition (const juce::ValueTree& tree, juce::Point<float>& result)
{
    const auto readCoordinate = [&tree] (const juce::Identifier& property, float fallback, float& coordinate)
    {
        if (! tree.hasProperty (property))
        {
            coordinate = fallback;
            return true;
        }

        const auto value = tree.getProperty (property);

        double parsed = 0.0;

        if (value.isInt() || value.isInt64() || value.isDouble())
            parsed = static_cast<double> (value);
        else if (! value.isString() || ! parseNumber (value.toString(), parsed))
            return false;

        coordinate = static_cast<float> (parsed);
        return std::isfinite (coordinate);
    };

    return readCoordinate ("x", 100.0f, result.x)
        && readCoordinate ("y", 100.0f, result.y);
}

std::optional<GraphNodeRole> parseNodeRole (const juce::ValueTree& nodeState)
{
    if (! nodeState.hasProperty ("role"))
        return std::nullopt;

    const auto role = nodeState.getProperty ("role").toString();

    if (role == "input")  return GraphNodeRole::input;
    if (role == "output") return GraphNodeRole::output;
    if (role == "module") return GraphNodeRole::module;
    return std::nullopt;
}

// Reconstruct the logical document through its validation API instead of
// trusting serialized nodes and connections directly.
std::optional<ParsedGraphState> parseStoredGraphState (const juce::ValueTree& state)
{
    if (! state.hasType (stateTag))
        return std::nullopt;

    if (state.hasProperty ("version"))
    {
        juce::int64 version = 0;

        if (! readIntegerProperty (state, "version", version) || version != 1)
            return std::nullopt;
    }

    const auto nodes = state.getChildWithName (nodesTag);

    if (! nodes.isValid())
        return std::nullopt;

    ParsedGraphState parsed;
    std::set<GraphNodeUID> seenNodeUIDs;

    for (int i = 0; i < nodes.getNumChildren(); ++i)
    {
        const auto nodeState = nodes.getChild (i);
        GraphNodeUID uid = 0;
        juce::Point<float> position;
        const auto role = parseNodeRole (nodeState);

        if (! nodeState.hasType (nodeTag)
            || ! readNodeUID (nodeState, "uid", uid)
            || ! readPosition (nodeState, position)
            || ! role.has_value()
            || ! seenNodeUIDs.insert (uid).second)
        {
            return std::nullopt;
        }

        if (uid == inputNodeUID || uid == outputNodeUID)
        {
            const auto requiredRole = uid == inputNodeUID ? GraphNodeRole::input : GraphNodeRole::output;

            if (*role != requiredRole || ! parsed.document.setNodePosition (uid, position))
                return std::nullopt;

            continue;
        }

        if (*role != GraphNodeRole::module)
            return std::nullopt;

        const auto moduleId = nodeState.getProperty ("moduleId").toString();
        const auto* descriptor = ModuleRegistry::findModule (moduleId);

        if (descriptor == nullptr || ! parsed.document.restoreModuleNode (uid, *descriptor, position))
            return std::nullopt;

        if (const auto moduleState = nodeState.getChildWithName ("ModuleState"); moduleState.isValid())
        {
            if (! moduleState.hasProperty ("moduleId")
                || moduleState.getProperty ("moduleId").toString() != moduleId)
            {
                return std::nullopt;
            }

            parsed.moduleStates.emplace (uid, moduleState);
        }
    }

    if (seenNodeUIDs.find (inputNodeUID) == seenNodeUIDs.end()
        || seenNodeUIDs.find (outputNodeUID) == seenNodeUIDs.end())
    {
        return std::nullopt;
    }

    if (state.hasProperty ("nextModuleNode"))
    {
        GraphNodeUID nextModuleNode = 0;

        if (! readNodeUID (state, "nextModuleNode", nextModuleNode)
            || ! parsed.document.restoreNextModuleNodeUID (nextModuleNode))
        {
            return std::nullopt;
        }
    }

    const auto connections = state.getChildWithName (connectionsTag);

    if (! connections.isValid())
    {
        if (! parsed.document.addConnection ({ inputNodeUID, outputNodeUID }))
            return std::nullopt;

        return parsed;
    }

    for (int i = 0; i < connections.getNumChildren(); ++i)
    {
        const auto connectionState = connections.getChild (i);
        GraphConnectionDescription connection;

        if (! connectionState.hasType (connectionTag)
            || ! readNodeUID (connectionState, "source", connection.source)
            || ! readNodeUID (connectionState, "destination", connection.destination)
            || ! parsed.document.addConnection (connection))
        {
            return std::nullopt;
        }
    }

    return parsed;
}
} // namespace

GraphAudioProcessor::GraphAudioProcessor()
    : AudioProcessor (BusesProperties().withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      audioGraph (std::make_unique<Graph>())
{
    audioGraph->setPlayConfigDetails (2, 2, currentSampleRate, currentBlockSize);
    buildDefaultGraph();
}

GraphAudioProcessor::~GraphAudioProcessor()
{
    if (audioGraph != nullptr)
        detachModuleProcessorListeners (*audioGraph);
}

void GraphAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const juce::ScopedLock lock (graphLock);

    if (graphPrepared)
    {
        audioGraph->releaseResources();
        graphPrepared = false;
    }

    currentSampleRate = sampleRate;
    currentBlockSize = juce::jmax (1, samplesPerBlock);

    audioGraph->setPlayConfigDetails (getTotalNumInputChannels(),
                                      getTotalNumOutputChannels(),
                                      currentSampleRate,
                                      currentBlockSize);

    if (! rebuildAudioGraphConnections (*audioGraph, graphDocument))
    {
        jassertfalse;
        return;
    }

    audioGraph->prepareToPlay (currentSampleRate, currentBlockSize);
    graphPrepared = true;
}

void GraphAudioProcessor::releaseResources()
{
    const juce::ScopedLock lock (graphLock);

    if (! graphPrepared)
        return;

    audioGraph->releaseResources();
    graphPrepared = false;
}

void GraphAudioProcessor::reset()
{
    const juce::ScopedLock lock (graphLock);

    if (graphPrepared)
        audioGraph->reset();
}

void GraphAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    audioGraph->processBlock (buffer, midiMessages);

    // Prevent invalid floating-point values from escaping into downstream host processing.
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            samples[sample] = sanitiseAudioSample (samples[sample]);
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

    return input == output
        && (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo());
}

bool GraphAudioProcessor::addModuleNode (const juce::String& moduleId,
                                         juce::Point<float> position,
                                         GraphNodeUID& createdNode)
{
    {
        const juce::ScopedLock lock (graphLock);
        const auto* descriptor = ModuleRegistry::findModule (moduleId);

        if (descriptor == nullptr)
            return false;

        // Validate on a copy; publish the document only after runtime creation succeeds.
        auto candidate = graphDocument;
        GraphNodeUID uid = 0;

        if (! candidate.addModuleNode (*descriptor, position, uid))
            return false;

        auto processor = ModuleRegistry::createProcessor (moduleId);
        auto* processorToObserve = processor.get();

        if (processor == nullptr || ! addModuleNodeToRuntime (*audioGraph, uid, std::move (processor)))
            return false;

        graphDocument = std::move (candidate);
        createdNode = uid;
        processorToObserve->addListener (this);
        audioGraph->rebuild();
    }

    sendChangeMessage();
    notifyHostStateChanged();
    return true;
}

bool GraphAudioProcessor::removeGraphNode (GraphNodeUID uid)
{
    {
        const juce::ScopedLock lock (graphLock);
        auto candidate = graphDocument;

        if (! candidate.removeNode (uid))
            return false;

        auto removedNode = audioGraph->removeNode (toNodeID (uid), Graph::UpdateKind::none);

        if (removedNode == nullptr)
            return false;

        if (auto* processor = dynamic_cast<AudioModuleProcessor*> (removedNode->getProcessor()))
            processor->removeListener (this);

        graphDocument = std::move (candidate);
        audioGraph->rebuild();
    }

    sendChangeMessage();
    notifyHostStateChanged();
    return true;
}

bool GraphAudioProcessor::addGraphConnection (GraphNodeUID source, GraphNodeUID destination)
{
    {
        const juce::ScopedLock lock (graphLock);
        auto candidate = graphDocument;

        if (! candidate.addConnection ({ source, destination }))
            return false;

        // Keep the previous document so channel-level runtime rebuild failures can roll back.
        const auto previous = graphDocument;
        graphDocument = std::move (candidate);

        if (! rebuildAudioGraphConnections (*audioGraph, graphDocument))
        {
            graphDocument = previous;

            if (! rebuildAudioGraphConnections (*audioGraph, graphDocument))
                jassertfalse;

            return false;
        }
    }

    sendChangeMessage();
    notifyHostStateChanged();
    return true;
}

bool GraphAudioProcessor::removeGraphConnection (GraphConnectionDescription connection)
{
    {
        const juce::ScopedLock lock (graphLock);
        auto candidate = graphDocument;

        if (! candidate.removeConnection (connection))
            return false;

        // Keep the previous document so channel-level runtime rebuild failures can roll back.
        const auto previous = graphDocument;
        graphDocument = std::move (candidate);

        if (! rebuildAudioGraphConnections (*audioGraph, graphDocument))
        {
            graphDocument = previous;

            if (! rebuildAudioGraphConnections (*audioGraph, graphDocument))
                jassertfalse;

            return false;
        }
    }

    sendChangeMessage();
    notifyHostStateChanged();
    return true;
}

void GraphAudioProcessor::setGraphNodePosition (GraphNodeUID uid, juce::Point<float> position)
{
    const juce::ScopedLock lock (graphLock);
    graphDocument.setNodePosition (uid, position);
}

std::vector<GraphNodeDescription> GraphAudioProcessor::getGraphNodes() const
{
    const juce::ScopedLock lock (graphLock);
    return graphDocument.getNodes();
}

std::vector<GraphConnectionDescription> GraphAudioProcessor::getGraphConnections() const
{
    const juce::ScopedLock lock (graphLock);
    return graphDocument.getConnections();
}

GraphNodeDescription GraphAudioProcessor::getGraphNode (GraphNodeUID uid) const
{
    const juce::ScopedLock lock (graphLock);

    if (const auto* node = graphDocument.findNode (uid))
        return *node;

    return {};
}

ModuleProcessorHandle GraphAudioProcessor::getModuleProcessorForNode (GraphNodeUID uid) const
{
    const juce::ScopedLock lock (graphLock);

    if (auto* node = audioGraph->getNodeForId (toNodeID (uid)))
        return ModuleProcessorHandle { Graph::Node::Ptr { node } };

    return {};
}

void GraphAudioProcessor::loadNAMFileForNode (GraphNodeUID uid, const juce::File& file)
{
    if (auto processor = getModuleProcessorForNode (uid))
    {
        if (! processor->getDescriptor().canLoadNAM)
            return;

        processor->loadNAMFileAsync (file);
        notifyHostStateChanged();
    }
}

void GraphAudioProcessor::loadIRFileForNode (GraphNodeUID uid, const juce::File& file)
{
    if (auto processor = getModuleProcessorForNode (uid))
    {
        if (! processor->getDescriptor().canLoadIR)
            return;

        processor->loadIRFileAsync (file);
        notifyHostStateChanged();
    }
}

void GraphAudioProcessor::audioProcessorParameterChanged (juce::AudioProcessor* processor,
                                                          int parameterIndex,
                                                          float newValue)
{
    juce::ignoreUnused (processor, parameterIndex, newValue);
    notifyHostStateChanged();
}

void GraphAudioProcessor::audioProcessorChanged (
    juce::AudioProcessor* processor,
    const juce::AudioProcessorListener::ChangeDetails& details)
{
    juce::ignoreUnused (processor);

    if (details.nonParameterStateChanged)
        notifyHostStateChanged();
}

// Graph edits and loaded assets are non-parameter state, so explicitly mark the
// plug-in state dirty for the host.
void GraphAudioProcessor::notifyHostStateChanged()
{
    updateHostDisplay (juce::AudioProcessorListener::ChangeDetails {}
                           .withNonParameterStateChanged (true));
}

void GraphAudioProcessor::attachModuleProcessorListeners (Graph& runtime)
{
    for (auto* node : runtime.getNodes())
        if (auto* processor = dynamic_cast<AudioModuleProcessor*> (node->getProcessor()))
            processor->addListener (this);
}

void GraphAudioProcessor::detachModuleProcessorListeners (Graph& runtime)
{
    for (auto* node : runtime.getNodes())
        if (auto* processor = dynamic_cast<AudioModuleProcessor*> (node->getProcessor()))
            processor->removeListener (this);
}

void GraphAudioProcessor::buildDefaultGraph()
{
    const juce::ScopedLock lock (graphLock);

    audioGraph->clear (Graph::UpdateKind::none);
    graphDocument = GraphDocument {};

    const auto* input = graphDocument.findNode (inputNodeUID);
    const auto* output = graphDocument.findNode (outputNodeUID);
    const auto endpointsAdded = input != nullptr && output != nullptr
                             && addIONodeToRuntime (*audioGraph, *input)
                             && addIONodeToRuntime (*audioGraph, *output);

    if (! endpointsAdded || ! rebuildAudioGraphConnections (*audioGraph, graphDocument))
        jassertfalse;
}

bool GraphAudioProcessor::addIONodeToRuntime (Graph& runtime,
                                              const GraphNodeDescription& description)
{
    if (description.role != GraphNodeRole::input && description.role != GraphNodeRole::output)
        return false;

    const auto type = description.role == GraphNodeRole::input
                    ? Graph::AudioGraphIOProcessor::audioInputNode
                    : Graph::AudioGraphIOProcessor::audioOutputNode;

    auto node = runtime.addNode (std::make_unique<Graph::AudioGraphIOProcessor> (type),
                                 toNodeID (description.uid),
                                 Graph::UpdateKind::none);
    return node != nullptr;
}

bool GraphAudioProcessor::addModuleNodeToRuntime (Graph& runtime,
                                                  GraphNodeUID uid,
                                                  std::unique_ptr<AudioModuleProcessor> processor)
{
    if (processor == nullptr)
        return false;

    auto node = runtime.addNode (std::move (processor), toNodeID (uid), Graph::UpdateKind::none);
    return node != nullptr;
}

// Materialise validated logical edges as JUCE channel connections. A failed
// rebuild removes any partial wiring before returning.
bool GraphAudioProcessor::rebuildAudioGraphConnections (Graph& runtime,
                                                        const GraphDocument& document)
{
    const auto existingConnections = runtime.getConnections();

    for (const auto& connection : existingConnections)
        runtime.removeConnection (connection, Graph::UpdateKind::none);

    for (const auto& connection : document.getConnections())
    {
        if (! addAudioConnectionsFor (runtime, connection))
        {
            const auto partiallyRebuiltConnections = runtime.getConnections();

            for (const auto& partiallyRebuiltConnection : partiallyRebuiltConnections)
                runtime.removeConnection (partiallyRebuiltConnection, Graph::UpdateKind::none);

            runtime.rebuild();
            return false;
        }
    }

    runtime.rebuild();
    return true;
}

bool GraphAudioProcessor::addAudioConnectionsFor (Graph& runtime,
                                                   GraphConnectionDescription connection)
{
    const auto sourceChannels = getNodeOutputChannelCount (runtime, connection.source);
    const auto destinationChannels = getNodeInputChannelCount (runtime, connection.destination);

    if (sourceChannels <= 0 || destinationChannels <= 0)
        return false;

    std::vector<Graph::Connection> addedConnections;
    addedConnections.reserve (static_cast<size_t> (destinationChannels));

    // A logical edge expands to the required JUCE channel connections. Mono
    // sources are duplicated when the destination exposes more channels.
    for (int destinationChannel = 0; destinationChannel < destinationChannels; ++destinationChannel)
    {
        const auto sourceChannel = juce::jmin (destinationChannel, sourceChannels - 1);

        const Graph::Connection graphConnection
        {
            { toNodeID (connection.source), sourceChannel },
            { toNodeID (connection.destination), destinationChannel }
        };

        if (! runtime.addConnection (graphConnection, Graph::UpdateKind::none))
        {
            for (const auto& addedConnection : addedConnections)
                runtime.removeConnection (addedConnection, Graph::UpdateKind::none);

            return false;
        }

        addedConnections.push_back (graphConnection);
    }

    return true;
}

int GraphAudioProcessor::getNodeOutputChannelCount (const Graph& runtime, GraphNodeUID uid)
{
    if (auto* node = runtime.getNodeForId (toNodeID (uid)))
        return node->getProcessor()->getTotalNumOutputChannels();

    return 0;
}

int GraphAudioProcessor::getNodeInputChannelCount (const Graph& runtime, GraphNodeUID uid)
{
    if (auto* node = runtime.getNodeForId (toNodeID (uid)))
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

// Persist the logical graph and module state; JUCE runtime details are rebuilt on restore.
void GraphAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const juce::ScopedLock lock (graphLock);

    juce::ValueTree state (stateTag);
    state.setProperty ("version", 1, nullptr);
    state.setProperty ("nextModuleNode", (int) graphDocument.getNextModuleNodeUID(), nullptr);

    juce::ValueTree nodes (nodesTag);

    for (const auto& description : graphDocument.getNodes())
    {
        juce::ValueTree nodeState (nodeTag);
        nodeState.setProperty ("uid", (int) description.uid, nullptr);
        nodeState.setProperty ("role", roleToString (description.role), nullptr);
        nodeState.setProperty ("moduleId", description.moduleId, nullptr);
        nodeState.setProperty ("x", description.x, nullptr);
        nodeState.setProperty ("y", description.y, nullptr);

        if (auto* runtimeNode = audioGraph->getNodeForId (toNodeID (description.uid)))
        {
            if (auto* processor = dynamic_cast<AudioModuleProcessor*> (runtimeNode->getProcessor()))
                nodeState.addChild (processor->createModuleState(), -1, nullptr);
        }

        nodes.addChild (nodeState, -1, nullptr);
    }

    juce::ValueTree connections (connectionsTag);

    for (const auto& connection : graphDocument.getConnections())
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

// Restore transactionally: build a complete candidate first and replace the
// active graph only after validation, connection building and preparation succeed.
void GraphAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    auto parsed = parseStoredGraphState (juce::ValueTree::fromXml (*xml));

    if (! parsed.has_value())
        return;

    auto restoredDocument = parsed->document;

    std::map<GraphNodeUID, std::unique_ptr<AudioModuleProcessor>> restoredProcessors;

    for (const auto& node : restoredDocument.getNodes())
    {
        if (node.role != GraphNodeRole::module)
            continue;

        auto processor = ModuleRegistry::createProcessor (node.moduleId);

        if (processor == nullptr)
            return;

        if (const auto stateForNode = parsed->moduleStates.find (node.uid);
            stateForNode != parsed->moduleStates.end())
        {
            processor->restoreModuleState (stateForNode->second);
        }

        restoredProcessors.emplace (node.uid, std::move (processor));
    }

    const auto wasSuspended = isSuspended();
    suspendProcessing (true);
    auto stateApplied = false;

    {
        const juce::ScopeGuard restoreSuspension { [this, wasSuspended]
        {
            suspendProcessing (wasSuspended);
        } };

        const juce::ScopedLock lock (graphLock);
        const auto shouldReprepare = graphPrepared;
        auto restoredGraph = std::make_unique<Graph>();
        restoredGraph->setPlayConfigDetails (getTotalNumInputChannels(),
                                              getTotalNumOutputChannels(),
                                              currentSampleRate,
                                              currentBlockSize);

        const auto* input = restoredDocument.findNode (inputNodeUID);
        const auto* output = restoredDocument.findNode (outputNodeUID);
        auto runtimeBuilt = input != nullptr && output != nullptr
                         && addIONodeToRuntime (*restoredGraph, *input)
                         && addIONodeToRuntime (*restoredGraph, *output);

        for (const auto& node : restoredDocument.getNodes())
        {
            if (! runtimeBuilt || node.role != GraphNodeRole::module)
                continue;

            auto processor = restoredProcessors.find (node.uid);
            runtimeBuilt = processor != restoredProcessors.end()
                        && addModuleNodeToRuntime (*restoredGraph,
                                                  node.uid,
                                                  std::move (processor->second));
        }

        runtimeBuilt = runtimeBuilt
                    && rebuildAudioGraphConnections (*restoredGraph, restoredDocument);

        if (runtimeBuilt && shouldReprepare)
            restoredGraph->prepareToPlay (currentSampleRate, currentBlockSize);

        if (! runtimeBuilt)
            return;

        // Only a complete candidate reaches this point, so swapping cannot expose a half-built graph.
        auto previousGraph = std::move (audioGraph);
        attachModuleProcessorListeners (*restoredGraph);
        detachModuleProcessorListeners (*previousGraph);
        audioGraph = std::move (restoredGraph);
        graphDocument = std::move (restoredDocument);
        graphPrepared = shouldReprepare;

        if (shouldReprepare)
            previousGraph->releaseResources();

        stateApplied = true;
    }

    if (stateApplied)
        sendChangeMessage();
}
} // namespace better
