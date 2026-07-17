#pragma once

#include "audio/modules/ModuleProcessor.h"
#include <juce_dsp/juce_dsp.h>

namespace better
{
class AmpProcessor final : public AudioModuleProcessor
{
public:
    AmpProcessor();

    static ModuleDescriptor createDescriptor();

private:
    void onPrepared (double sampleRate, int samplesPerBlock) override;
    void onReset() override;
    void processAudio (juce::AudioBuffer<float>& buffer, int numSamples) override;

    juce::dsp::Gain<float> inputGain;
    juce::dsp::WaveShaper<float> waveShaper;
    juce::dsp::StateVariableTPTFilter<float> toneFilter;
    juce::dsp::Gain<float> outputLevel;

    // Rohpointer auf APVTS-Parameter; Besitz bleibt bei AudioProcessorValueTreeState.
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* levelParam = nullptr;
};
} // namespace better
