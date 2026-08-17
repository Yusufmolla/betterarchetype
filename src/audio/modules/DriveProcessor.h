#pragma once

#include "audio/modules/ModuleProcessor.h"

#include <array>
#include <atomic>

namespace better
{
class DriveProcessor final : public AudioModuleProcessor
{
public:
    DriveProcessor();

    static ModuleDescriptor createDescriptor();

private:
    void onReset() override;
    void processAudio (juce::AudioBuffer<float>& buffer, int numSamples) override;

    float processSample (int channel,
                         float sample,
                         float driveGain,
                         float tone,
                         float lowpassCoefficient,
                         float levelGain);

    std::array<float, 2> toneState {};
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* levelParam = nullptr;
};
} // namespace better
