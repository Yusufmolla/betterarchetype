 #include "audio/modules/AmpProcessor.h"

#include <cmath>

namespace better
{
AmpProcessor::AmpProcessor()
    : AudioModuleProcessor (createDescriptor())
{
    // Parameter-Pointer cachen; processAudio läuft im Audio-Thread.
    driveParam = getParameters().getRawParameterValue ("drive");
    toneParam  = getParameters().getRawParameterValue ("tone");
    levelParam = getParameters().getRawParameterValue ("level");

    // Einfaches Soft-Clipping für das Basic-Amp-Modul.
    waveShaper.functionToUse = [] (float x) {
        return std::tanh (x);
    };
}

ModuleDescriptor AmpProcessor::createDescriptor()
{
    return
    {
        "amp_basic",
        "Basic Tube Amp",
        "AMP",
        {
            toggleControl ("enabled", "Enabled", true),
            unitSlider ("drive", "Drive", 0.5f),
            unitSlider ("tone", "Tone", 0.5f),
            gainSlider ("level", "Level", 0.0f)
        },
        false,
        false
    };
}

void AmpProcessor::onPrepared (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32> (getTotalNumInputChannels());

    inputGain.prepare (spec);
    waveShaper.prepare (spec);
    toneFilter.prepare (spec);
    outputLevel.prepare (spec);

    toneFilter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
}

void AmpProcessor::onReset()
{
    inputGain.reset();
    waveShaper.reset();
    toneFilter.reset();
    outputLevel.reset();
}

void AmpProcessor::processAudio (juce::AudioBuffer<float>& buffer, int numSamples)
{
    juce::ignoreUnused (numSamples);

    // Drive und Tone sind 0..1 GUI-Werte und werden auf Audio-Werte gemappt.
    if (driveParam != nullptr)
    {
        float mappedDriveDb = juce::jmap (driveParam->load(), 0.0f, 1.0f, 0.0f, 40.0f);
        inputGain.setGainDecibels (mappedDriveDb);
    }

    if (toneParam != nullptr)
    {
        float cutoff = juce::jmap (toneParam->load(), 0.0f, 1.0f, 1000.0f, 10000.0f);
        toneFilter.setCutoffFrequency (cutoff);
    }

    if (levelParam != nullptr)
    {
        outputLevel.setGainDecibels (levelParam->load());
    }

    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> context (block);

    inputGain.process (context);
    waveShaper.process (context);
    toneFilter.process (context);
    outputLevel.process (context);
}

} // namespace better
