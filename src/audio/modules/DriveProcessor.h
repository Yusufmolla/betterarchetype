#pragma once

#include "audio/modules/ModuleProcessor.h"

#include <vector>

namespace better
{
class DriveProcessor final : public AudioModuleProcessor
{
public:
    DriveProcessor();

    static ModuleDescriptor createDescriptor();

private:
    void onPrepared (double sampleRate, int samplesPerBlock) override;
    void onReset() override;
    void processAudio (juce::AudioBuffer<float>& buffer, int numSamples) override;

    float processSample (int channel, float sample, float drive, float tone, float levelGain);

    std::vector<float> toneState;
};
} // namespace better
