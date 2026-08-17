#pragma once

#include "audio/modules/ModuleProcessor.h"
#include "audio/modules/NAMModelLoader.h"

#include <atomic>
#include <cstdint>

namespace better
{
/** Graph module that wraps Neural Amp Modeler processing.

    Model construction runs on a single background worker. A monotonically
    increasing revision identifies the newest request so stale asynchronous
    loads cannot become the published module state.
*/
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

    void clearLoadedModel (juce::String reason);

    NAMModelLoader namLoader;
    std::atomic<bool> modelLoaded { false };
    // Shared loader/status bookkeeping is locked; the realtime loaded flag is atomic.
    juce::CriticalSection stateLock;
    std::uint64_t loadRevision = 0;
    juce::String statusText = "No model loaded";
    juce::File requestedModelFile;

    // Cached non-owning APVTS parameter pointers avoid lookups on the audio thread.
    std::atomic<float>* inputParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    juce::ThreadPool loaderPool { 1 };
};
} // namespace better
