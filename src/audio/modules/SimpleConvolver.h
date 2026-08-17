#pragma once

#include <juce_dsp/juce_dsp.h>

#include <atomic>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace better
{
class SimpleConvolver
{
public:
    SimpleConvolver()
        : convolution (createConvolution())
    {
    }

    void prepare (double sampleRate, int numChannels, int expectedBufferSize)
    {
        const std::lock_guard<std::mutex> lock (convolutionMutex);
        const auto safeBufferSize = juce::jmax (1, expectedBufferSize);

        currentSpec =
        {
            sampleRate,
            static_cast<juce::uint32> (safeBufferSize),
            static_cast<juce::uint32> (juce::jlimit (1, 2, numChannels))
        };

        prepared.store (false, std::memory_order_release);
        convolution->prepare (currentSpec);
        maximumBlockSize = static_cast<size_t> (safeBufferSize);
        prepared.store (true, std::memory_order_release);
    }

    void setImpulseResponse (const std::vector<float>& ir,
                             int irSampleRate,
                             float newOutputGain = 1.0f)
    {
        if (ir.empty() || ir.size() > static_cast<size_t> (std::numeric_limits<int>::max()))
        {
            clear();
            return;
        }

        const auto irSize = static_cast<int> (ir.size());
        juce::AudioBuffer<float> irBuffer (1, irSize);
        irBuffer.copyFrom (0, 0, ir.data(), irSize);

        auto candidate = createConvolution();
        candidate->loadImpulseResponse (std::move (irBuffer),
                                        static_cast<double> (juce::jmax (1, irSampleRate)),
                                        juce::dsp::Convolution::Stereo::no,
                                        juce::dsp::Convolution::Trim::yes,
                                        juce::dsp::Convolution::Normalise::no);

        const std::lock_guard<std::mutex> lock (convolutionMutex);

        // prepare() waits for the most recently supplied IR to become active.
        // Preparing a candidate prevents the new gain from being paired with
        // the old asynchronous IR; the ready convolution and gain are published together.
        if (prepared.load (std::memory_order_acquire))
            candidate->prepare (currentSpec);

        convolution = std::move (candidate);
        outputGain = std::isfinite (newOutputGain) ? newOutputGain : 1.0f;
        loaded.store (true, std::memory_order_release);
    }

    void clear()
    {
        auto emptyConvolution = createConvolution();
        const std::lock_guard<std::mutex> lock (convolutionMutex);

        if (prepared.load (std::memory_order_acquire))
            emptyConvolution->prepare (currentSpec);

        convolution = std::move (emptyConvolution);
        outputGain = 1.0f;
        loaded.store (false, std::memory_order_release);
    }

    void reset()
    {
        const std::lock_guard<std::mutex> lock (convolutionMutex);
        convolution->reset();
    }

    bool process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept
    {
        if (! prepared.load (std::memory_order_acquire)
            || ! loaded.load (std::memory_order_acquire)
            || numSamples <= 0
            || numSamples > buffer.getNumSamples())
        {
            return false;
        }

        std::unique_lock<std::mutex> lock (convolutionMutex, std::try_to_lock);

        if (! lock.owns_lock() || ! loaded.load (std::memory_order_acquire))
            return false;

        auto block = juce::dsp::AudioBlock<float> (buffer).getSubBlock (0, static_cast<size_t> (numSamples));
        const auto chunkCapacity = juce::jmax (static_cast<size_t> (1), maximumBlockSize);

        for (size_t offset = 0; offset < block.getNumSamples(); offset += chunkCapacity)
        {
            auto chunk = block.getSubBlock (offset,
                                            juce::jmin (chunkCapacity,
                                                        block.getNumSamples() - offset));
            juce::dsp::ProcessContextReplacing<float> context (chunk);
            convolution->process (context);
            chunk.multiplyBy (outputGain);
        }

        return true;
    }

    bool hasImpulseResponse() const noexcept
    {
        return loaded.load (std::memory_order_acquire);
    }

private:
    static constexpr size_t convolutionHeadSize = 256;

    static std::unique_ptr<juce::dsp::Convolution> createConvolution()
    {
        return std::make_unique<juce::dsp::Convolution> (
            juce::dsp::Convolution::NonUniform { convolutionHeadSize });
    }

    std::unique_ptr<juce::dsp::Convolution> convolution;
    juce::dsp::ProcessSpec currentSpec { 48000.0, 1, 1 };
    std::atomic<bool> loaded { false };
    std::mutex convolutionMutex;
    std::atomic<bool> prepared { false };
    float outputGain = 1.0f;
    size_t maximumBlockSize = 1;
};
} // namespace better
