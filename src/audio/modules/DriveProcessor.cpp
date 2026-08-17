#include "audio/modules/DriveProcessor.h"

#include <algorithm>
#include <cmath>

namespace better
{
namespace
{
constexpr auto minimumDriveGain = 1.0f;
constexpr auto maximumDriveGain = 28.0f;
constexpr auto minimumToneCoefficient = 0.025f;
constexpr auto maximumToneCoefficient = 0.38f;
constexpr auto highFrequencyEmphasis = 0.75f;
constexpr auto shapedSampleLimit = 2.0f;
constexpr auto outputSampleLimit = 3.0f;
} // namespace

DriveProcessor::DriveProcessor()
    : AudioModuleProcessor (createDescriptor())
{
    driveParam = getParameters().getRawParameterValue ("drive");
    toneParam = getParameters().getRawParameterValue ("tone");
    levelParam = getParameters().getRawParameterValue ("level");
}

ModuleDescriptor DriveProcessor::createDescriptor()
{
    return
    {
        "drive",
        "Drive",
        "FX",
        {
            toggleControl ("enabled", "Enabled", true),
            unitSlider ("drive", "Drive", 0.35f),
            unitSlider ("tone", "Tone", 0.55f),
            gainSlider ("level", "Level", 0.0f)
        },
        false,
        false
    };
}

void DriveProcessor::onReset()
{
    std::fill (toneState.begin(), toneState.end(), 0.0f);
}

void DriveProcessor::processAudio (juce::AudioBuffer<float>& buffer, int numSamples)
{
    const auto drive = juce::jlimit (0.0f, 1.0f,
                                     driveParam != nullptr ? driveParam->load() : 0.35f);
    const auto tone = juce::jlimit (0.0f, 1.0f,
                                    toneParam != nullptr ? toneParam->load() : 0.55f);
    const auto levelDb = levelParam != nullptr ? levelParam->load() : 0.0f;
    const auto levelGain = juce::Decibels::decibelsToGain (levelDb);
    const auto driveGain = juce::jmap (drive, minimumDriveGain, maximumDriveGain);
    const auto lowpassCoefficient = juce::jmap (tone,
                                                minimumToneCoefficient,
                                                maximumToneCoefficient);
    const auto channelsToProcess = juce::jmin (buffer.getNumChannels(),
                                                static_cast<int> (toneState.size()));

    jassert (buffer.getNumChannels() <= static_cast<int> (toneState.size()));

    for (int channel = 0; channel < channelsToProcess; ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
            samples[sample] = processSample (channel,
                                             samples[sample],
                                             driveGain,
                                             tone,
                                             lowpassCoefficient,
                                             levelGain);
    }
}

float DriveProcessor::processSample (int channel,
                                     float sample,
                                     float driveGain,
                                     float tone,
                                     float lowpassCoefficient,
                                     float levelGain)
{
    sample = sanitiseAudioSample (sample);
    const auto shaped = std::tanh (sample * driveGain);

    auto& lowState = toneState[(size_t) channel];
    lowState += lowpassCoefficient * (shaped - lowState);

    const auto dark = lowState;
    const auto bright = juce::jlimit (-shapedSampleLimit,
                                      shapedSampleLimit,
                                      shaped + ((shaped - lowState) * highFrequencyEmphasis));
    const auto toned = dark + ((bright - dark) * tone);

    return juce::jlimit (-outputSampleLimit, outputSampleLimit, toned * levelGain);
}
} // namespace better
