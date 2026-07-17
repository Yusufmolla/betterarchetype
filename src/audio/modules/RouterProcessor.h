#pragma once

#include "audio/modules/ModuleProcessor.h"

namespace better
{
class RouterProcessor final : public AudioModuleProcessor
{
public:
    RouterProcessor();

    static ModuleDescriptor createDescriptor();

private:
    void processAudio (juce::AudioBuffer<float>& buffer, int numSamples) override;

    // Rohpointer auf APVTS-Parameter; Besitz bleibt bei AudioProcessorValueTreeState.
    std::atomic<float>* monoParam = nullptr;
};
} // namespace better
