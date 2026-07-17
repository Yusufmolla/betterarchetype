#pragma once

#include "audio/graph/GraphTypes.h"
#include "audio/modules/ModuleProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <vector>

namespace better
{
class GraphAudioProcessor final : public juce::AudioProcessor,
                                  public juce::ChangeBroadcaster
{
public:
    GraphAudioProcessor();
    ~GraphAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
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

    bool addModuleNode (const juce::String& moduleId, juce::Point<float> position, GraphNodeUID& createdNode);
    bool removeGraphNode (GraphNodeUID uid);
    bool addGraphConnection (GraphNodeUID source, GraphNodeUID destination);
    bool removeGraphConnection (GraphConnectionDescription connection);
    void setGraphNodePosition (GraphNodeUID uid, juce::Point<float> position);

    std::vector<GraphNodeDescription> getGraphNodes() const;
    std::vector<GraphConnectionDescription> getGraphConnections() const;
    GraphNodeDescription getGraphNode (GraphNodeUID uid) const;
    AudioModuleProcessor* getModuleProcessorForNode (GraphNodeUID uid) const;

    void loadNAMFileForNode (GraphNodeUID uid, const juce::File& file);
    void loadIRFileForNode (GraphNodeUID uid, const juce::File& file);

private:
    using Graph = juce::AudioProcessorGraph;
    using NodeID = Graph::NodeID;

    struct NodeRecord
    {
        GraphNodeDescription description;
    };

    void buildDefaultGraph();
    bool addIONode (GraphNodeUID uid, GraphNodeRole role, juce::Point<float> position);
    bool addModuleNodeInternal (GraphNodeUID uid,
                                const juce::String& moduleId,
                                juce::Point<float> position,
                                const juce::ValueTree* moduleState);
    bool canAddLogicalConnection (GraphConnectionDescription connection) const;
    bool addLogicalConnectionInternal (GraphConnectionDescription connection);
    void rebuildAudioGraphConnections();
    bool addAudioConnectionsFor (GraphConnectionDescription connection);
    int getNodeOutputChannelCount (GraphNodeUID uid) const;
    int getNodeInputChannelCount (GraphNodeUID uid) const;
    static NodeID toNodeID (GraphNodeUID uid) { return NodeID { uid }; }
    static juce::String roleToString (GraphNodeRole role);
    static GraphNodeRole stringToRole (const juce::String& role);

    Graph audioGraph;
    mutable juce::CriticalSection graphLock;
    std::map<GraphNodeUID, NodeRecord> nodeRecords;
    std::vector<GraphConnectionDescription> logicalConnections;
    GraphNodeUID nextModuleNode = firstModuleNodeUID;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GraphAudioProcessor)
};
} // namespace better
