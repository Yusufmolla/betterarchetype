#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace better
{
class SimpleConvolver
{
public:
    void prepare (double sampleRate, int numChannels, int expectedBufferSize)
    {
        const std::lock_guard<std::mutex> lock (convolutionMutex);

        const juce::dsp::ProcessSpec spec
        {
            sampleRate,
            static_cast<juce::uint32> (juce::jmax (1, expectedBufferSize)),
            static_cast<juce::uint32> (juce::jmax (1, numChannels))
        };

        convolution.prepare (spec);
        prepared = true;
    }

    void setImpulseResponse (const std::vector<float>& ir, int irSampleRate)
    {
        juce::AudioBuffer<float> irBuffer (1, (int) ir.size());

        if (! ir.empty())
            irBuffer.copyFrom (0, 0, ir.data(), (int) ir.size());

        const std::lock_guard<std::mutex> lock (convolutionMutex);

        convolution.loadImpulseResponse (std::move (irBuffer),
                                         (double) juce::jmax (1, irSampleRate),
                                         juce::dsp::Convolution::Stereo::no,
                                         juce::dsp::Convolution::Trim::yes,
                                         juce::dsp::Convolution::Normalise::no);
        convolution.reset();
        loaded.store (! ir.empty(), std::memory_order_release);
    }

    void clear()
    {
        const std::lock_guard<std::mutex> lock (convolutionMutex);
        convolution.reset();
        loaded.store (false, std::memory_order_release);
    }

    void process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        if (! prepared || ! loaded.load (std::memory_order_acquire) || numSamples <= 0)
            return;

        std::unique_lock<std::mutex> lock (convolutionMutex, std::try_to_lock);

        if (! lock.owns_lock())
            return;

        auto block = juce::dsp::AudioBlock<float> (buffer).getSubBlock (0, (size_t) numSamples);
        juce::dsp::ProcessContextReplacing<float> context (block);
        convolution.process (context);
    }

    bool hasImpulseResponse() const
    {
        return loaded.load (std::memory_order_acquire);
    }

private:
    juce::dsp::Convolution convolution { juce::dsp::Convolution::NonUniform { 256 } };
    std::atomic<bool> loaded { false };
    std::mutex convolutionMutex;
    bool prepared = false;
};
} // namespace better
