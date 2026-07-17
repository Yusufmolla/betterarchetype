#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <memory>
#include <vector>

namespace better
{
class IRLoader
{
public:
    struct IRData
    {
        juce::String name;
        std::vector<float> samples;
        int sampleRate = 44100;
    };

    static std::unique_ptr<IRData> loadIRFile (const juce::File& irFile)
    {
        if (! irFile.existsAsFile())
            return {};

        juce::AudioFormatManager formatManager;
        formatManager.registerBasicFormats();

        auto reader = std::unique_ptr<juce::AudioFormatReader> (formatManager.createReaderFor (irFile));

        if (reader == nullptr || reader->lengthInSamples <= 0)
            return {};

        juce::AudioBuffer<float> buffer ((int) reader->numChannels, (int) reader->lengthInSamples);
        reader->read (&buffer, 0, (int) reader->lengthInSamples, 0, true, true);

        auto ir = std::make_unique<IRData>();
        ir->name = irFile.getFileNameWithoutExtension();
        ir->sampleRate = (int) reader->sampleRate;
        ir->samples.resize ((size_t) buffer.getNumSamples());

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        {
            auto mono = 0.0f;

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
                mono += buffer.getSample (channel, sample);

            ir->samples[(size_t) sample] = mono / static_cast<float> (juce::jmax (1, buffer.getNumChannels()));
        }

        return ir;
    }
};
} // namespace better
