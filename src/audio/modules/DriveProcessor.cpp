#include "audio/modules/DriveProcessor.h"

#include <cmath>

namespace better
{
DriveProcessor::DriveProcessor()
    : AudioModuleProcessor (createDescriptor())
{
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

void DriveProcessor::onPrepared (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused (sampleRate, samplesPerBlock);
    toneState.assign ((size_t) juce::jmax (1, getTotalNumOutputChannels()), 0.0f);
}

void DriveProcessor::onReset()
{
    std::fill (toneState.begin(), toneState.end(), 0.0f);
}

void DriveProcessor::processAudio (juce::AudioBuffer<float>& buffer, int numSamples)
{
    const auto drive = getFloatParameter ("drive", 0.35f);
    const auto tone = getFloatParameter ("tone", 0.55f);
    const auto levelGain = juce::Decibels::decibelsToGain (getFloatParameter ("level", 0.0f));

    if ((int) toneState.size() < buffer.getNumChannels())
        toneState.resize ((size_t) buffer.getNumChannels(), 0.0f);

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        auto* samples = buffer.getWritePointer (channel);

        for (int sample = 0; sample < numSamples; ++sample)
            samples[sample] = processSample (channel, samples[sample], drive, tone, levelGain);
    }
}

float DriveProcessor::processSample (int channel, float sample, float drive, float tone, float levelGain)
{
    sample = sanitiseAudioSample (sample);
    drive = juce::jlimit (0.0f, 1.0f, drive);
    tone = juce::jlimit (0.0f, 1.0f, tone);

    const auto driveGain = juce::jmap (drive, 1.0f, 28.0f);
    const auto shaped = std::tanh (sample * driveGain);

    auto& lowState = toneState[(size_t) channel];
    const auto lowpassCoefficient = juce::jmap (tone, 0.025f, 0.38f);
    lowState += lowpassCoefficient * (shaped - lowState);

    const auto dark = lowState;
    const auto bright = juce::jlimit (-2.0f, 2.0f, shaped + ((shaped - lowState) * 0.75f));
    const auto toned = dark + ((bright - dark) * tone);

    return juce::jlimit (-3.0f, 3.0f, toned * levelGain);
}
} // namespace better
