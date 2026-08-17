#pragma once

#include "audio/graph/GraphDocument.h"
#include "audio/graph/ModuleProcessorHandle.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace better
{
/** Main plug-in processor and bridge between logical and realtime graph state.

    GraphDocument is the validated source of truth. This class mirrors it into
    a juce::AudioProcessorGraph, owns the runtime module processors and restores
    saved state through a fully built candidate graph before activation.
*/
class GraphAudioProcessor final : public juce::AudioProcessor,
                                  public juce::ChangeBroadcaster,
                                  private juce::AudioProcessorListener
{
public:
    GraphAudioProcessor();
    ~GraphAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "BetterArchetype"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Editor-facing graph mutations keep the logical document and runtime graph
    // in sync as one operation.
    bool addModuleNode (const juce::String& moduleId, juce::Point<float> position, GraphNodeUID& createdNode);
    bool removeGraphNode (GraphNodeUID uid);
    bool addGraphConnection (GraphNodeUID source, GraphNodeUID destination);
    bool removeGraphConnection (GraphConnectionDescription connection);
    void setGraphNodePosition (GraphNodeUID uid, juce::Point<float> position);

    std::vector<GraphNodeDescription> getGraphNodes() const;
    std::vector<GraphConnectionDescription> getGraphConnections() const;
    GraphNodeDescription getGraphNode (GraphNodeUID uid) const;
    ModuleProcessorHandle getModuleProcessorForNode (GraphNodeUID uid) const;

    void loadNAMFileForNode (GraphNodeUID uid, const juce::File& file);
    void loadIRFileForNode (GraphNodeUID uid, const juce::File& file);

private:
    using Graph = juce::AudioProcessorGraph;
    using NodeID = Graph::NodeID;

    void audioProcessorParameterChanged (juce::AudioProcessor* processor,
                                         int parameterIndex,
                                         float newValue) override;
    void audioProcessorChanged (juce::AudioProcessor* processor,
                                const juce::AudioProcessorListener::ChangeDetails& details) override;

    void notifyHostStateChanged();
    void attachModuleProcessorListeners (Graph& runtime);
    void detachModuleProcessorListeners (Graph& runtime);
    void buildDefaultGraph();
    static bool addIONodeToRuntime (Graph& runtime, const GraphNodeDescription& description);
    static bool addModuleNodeToRuntime (Graph& runtime,
                                        GraphNodeUID uid,
                                        std::unique_ptr<AudioModuleProcessor> processor);
    static bool rebuildAudioGraphConnections (Graph& runtime, const GraphDocument& document);
    static bool addAudioConnectionsFor (Graph& runtime, GraphConnectionDescription connection);
    static int getNodeOutputChannelCount (const Graph& runtime, GraphNodeUID uid);
    static int getNodeInputChannelCount (const Graph& runtime, GraphNodeUID uid);
    static NodeID toNodeID (GraphNodeUID uid) { return NodeID { uid }; }
    static juce::String roleToString (GraphNodeRole role);

    std::unique_ptr<Graph> audioGraph;
    // Protects the document/runtime pair from partially observed mutations or restores.
    mutable juce::CriticalSection graphLock;
    GraphDocument graphDocument;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    bool graphPrepared = false; // Access only while graphLock is held.

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphAudioProcessor)
};
} // namespace better
