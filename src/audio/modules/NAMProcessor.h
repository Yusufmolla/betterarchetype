#pragma once

#include "audio/modules/ModuleProcessor.h"
#include "audio/modules/NAMModelLoader.h"

#include <atomic>

namespace better
{
class NAMProcessor final : public AudioModuleProcessor
{
public:
    NAMProcessor();
    ~NAMProcessor() override;

    static ModuleDescriptor createDescriptor();

    juce::String getStatusText() const override;
    void loadNAMFileAsync (const juce::File& file) override;

private:
    void onPrepared (double sampleRate, int samplesPerBlock) override;
    void processAudio (juce::AudioBuffer<float>& buffer, int numSamples) override;
    void writeExtraState (juce::ValueTree& state) override;
    void restoreExtraState (const juce::ValueTree& state) override;

    void requestDefaultModelLoad();
    void setStatus (juce::String newStatus);
    void clearLoadedModel (juce::String reason);

    NAMModelLoader namLoader;
    juce::ThreadPool loaderPool { 1 };
    std::atomic<int> loadRevision { 0 };
    std::atomic<bool> modelLoaded { false };
    juce::AudioBuffer<float> monoInputBuffer;
    juce::AudioBuffer<float> monoOutputBuffer;
    juce::CriticalSection statusLock;
    juce::String statusText = "No model loaded";
    juce::File currentModelFile;

    // Rohpointer auf APVTS-Parameter; Besitz bleibt bei AudioProcessorValueTreeState.
    std::atomic<float>* inputParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
};
} // namespace better
