#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace better
{
class IRLoader
{
public:
    struct IRData
    {
        std::vector<float> samples;
        int sampleRate = 48000;
    };

    static std::unique_ptr<IRData> loadIRFile (const juce::File& irFile) noexcept
    {
        if (! irFile.existsAsFile())
            return {};

        try
        {
            juce::AudioFormatManager formatManager;
            formatManager.registerBasicFormats();

            auto reader = std::unique_ptr<juce::AudioFormatReader> (formatManager.createReaderFor (irFile));

            if (reader == nullptr
                || reader->numChannels == 0
                || reader->numChannels > maximumIRChannels
                || ! std::isfinite (reader->sampleRate)
                || reader->sampleRate < minimumIRSampleRate
                || reader->sampleRate > maximumIRSampleRate
                || reader->lengthInSamples <= 0
                || reader->lengthInSamples > std::numeric_limits<int>::max())
            {
                return {};
            }

            const auto maximumSamples = static_cast<juce::int64> (
                std::ceil (reader->sampleRate * maximumIRDurationSeconds));

            if (reader->lengthInSamples > maximumSamples)
                return {};

            const auto numChannels = static_cast<int> (reader->numChannels);
            const auto numSamples = static_cast<int> (reader->lengthInSamples);
            juce::AudioBuffer<float> buffer (numChannels, numSamples);

            if (! reader->read (&buffer, 0, numSamples, 0, true, true))
                return {};

            auto ir = std::make_unique<IRData>();
            ir->sampleRate = juce::roundToInt (reader->sampleRate);
            ir->samples.resize (static_cast<size_t> (numSamples));

            for (int sample = 0; sample < numSamples; ++sample)
            {
                auto mono = 0.0f;

                for (int channel = 0; channel < numChannels; ++channel)
                    mono += buffer.getSample (channel, sample);

                mono /= static_cast<float> (numChannels);

                if (! std::isfinite (mono))
                    return {};

                ir->samples[static_cast<size_t> (sample)] = mono;
            }

            return ir;
        }
        catch (...)
        {
            return {};
        }
    }

private:
    static constexpr unsigned int maximumIRChannels = 8;
    static constexpr double minimumIRSampleRate = 8000.0;
    static constexpr double maximumIRSampleRate = 384000.0;
    static constexpr double maximumIRDurationSeconds = 2.0;
};
} // namespace better
