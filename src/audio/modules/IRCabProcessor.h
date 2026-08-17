#pragma once

#include "audio/modules/IRLoader.h"
#include "audio/modules/ModuleProcessor.h"
#include "audio/modules/SimpleConvolver.h"

#include <atomic>
#include <cstdint>

namespace better
{
class IRCabProcessor final : public AudioModuleProcessor
{
public:
    IRCabProcessor();
    ~IRCabProcessor() override;

    static ModuleDescriptor createDescriptor();
    static float calculateGatewayIRGain (double originalIRSampleRate) noexcept;

    juce::String getStatusText() const override;
    void loadIRFileAsync (const juce::File& file) override;

private:
    void onPrepared (double sampleRate, int samplesPerBlock) override;
    void onReset() override;
    void processAudio (juce::AudioBuffer<float>& buffer, int numSamples) override;
    void writeExtraState (juce::ValueTree& state) override;
    void restoreExtraState (const juce::ValueTree& state) override;

    void clearLoadedIR (juce::String reason);

    SimpleConvolver convolver;
    juce::CriticalSection stateLock;
    std::uint64_t loadRevision = 0;
    juce::String statusText = "No IR loaded";
    juce::File requestedIRFile;
    std::atomic<float>* levelParam = nullptr;
    juce::ThreadPool loaderPool { 1 };
};
} // namespace better
