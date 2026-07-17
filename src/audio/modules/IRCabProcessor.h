#pragma once

#include "audio/modules/IRLoader.h"
#include "audio/modules/ModuleProcessor.h"
#include "audio/modules/SimpleConvolver.h"

#include <atomic>

namespace better
{
class IRCabProcessor final : public AudioModuleProcessor
{
public:
    IRCabProcessor();
    ~IRCabProcessor() override;

    static ModuleDescriptor createDescriptor();

    juce::String getStatusText() const override;
    void loadIRFileAsync (const juce::File& file) override;

private:
    void onPrepared (double sampleRate, int samplesPerBlock) override;
    void processAudio (juce::AudioBuffer<float>& buffer, int numSamples) override;
    void writeExtraState (juce::ValueTree& state) override;
    void restoreExtraState (const juce::ValueTree& state) override;

    void setStatus (juce::String newStatus);
    void clearLoadedIR (juce::String reason);

    SimpleConvolver convolver;
    juce::ThreadPool loaderPool { 1 };
    std::atomic<int> loadRevision { 0 };
    std::atomic<bool> irLoaded { false };
    juce::CriticalSection statusLock;
    juce::String statusText = "No IR loaded";
    juce::File currentIRFile;
};
} // namespace better
