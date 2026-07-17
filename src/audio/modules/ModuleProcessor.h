#pragma once

#include "audio/graph/GraphTypes.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace better
{
class AudioModuleProcessor : public juce::AudioProcessor
{
public:
    explicit AudioModuleProcessor (ModuleDescriptor descriptor);
    ~AudioModuleProcessor() override = default;

    const juce::String getName() const override;
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;
    bool supportsDoublePrecisionProcessing() const override { return false; }

    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    const ModuleDescriptor& getDescriptor() const noexcept { return descriptor; }
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters; }
    const juce::AudioProcessorValueTreeState& getParameters() const noexcept { return parameters; }

    juce::ValueTree createModuleState();
    void restoreModuleState (const juce::ValueTree& state);

    virtual juce::String getStatusText() const;
    virtual void loadNAMFileAsync (const juce::File& file);
    virtual void loadIRFileAsync (const juce::File& file);

protected:
    virtual void processAudio (juce::AudioBuffer<float>& buffer, int numSamples) = 0;
    virtual void onPrepared (double sampleRate, int samplesPerBlock);
    virtual void onReset() {}
    virtual void writeExtraState (juce::ValueTree& state);
    virtual void restoreExtraState (const juce::ValueTree& state);

    bool isModuleEnabled() const;
    float getFloatParameter (const juce::String& parameterId, float fallback = 0.0f) const;

    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout (const std::vector<ModuleControlDescriptor>& controls);

    ModuleDescriptor descriptor;
    juce::AudioProcessorValueTreeState parameters;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AudioModuleProcessor)
};
} // namespace better
