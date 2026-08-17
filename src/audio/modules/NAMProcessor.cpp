#include "audio/modules/NAMProcessor.h"

#include <utility>

namespace better
{
NAMProcessor::NAMProcessor()
    : AudioModuleProcessor (createDescriptor())
{
    // Cache parameter pointers once; APVTS retains ownership for the processor lifetime.
    inputParam = getParameters().getRawParameterValue ("input");
    outputParam = getParameters().getRawParameterValue ("output");
}

NAMProcessor::~NAMProcessor()
{
    // The parser cannot be interrupted while NAMCore is constructing a model.
    // Wait until the active job has stopped before any captured members are destroyed.
    loaderPool.removeAllJobs (true, -1);
}

ModuleDescriptor NAMProcessor::createDescriptor()
{
    return
    {
        "nam",
        "NAM Amp",
        "AMP",
        {
            toggleControl ("enabled", "Enabled", true),
            gainSlider ("input", "Input", 0.0f, -18.0f, 18.0f),
            gainSlider ("output", "Output", 0.0f, -24.0f, 12.0f)
        },
        true,
        false
    };
}

juce::String NAMProcessor::getStatusText() const
{
    const juce::ScopedLock lock (stateLock);
    return statusText;
}

void NAMProcessor::loadNAMFileAsync (const juce::File& file)
{
    if (! file.existsAsFile())
    {
        clearLoadedModel ("No NAM model loaded");
        return;
    }

    std::uint64_t revision = 0;

    {
        const juce::ScopedLock lock (stateLock);
        revision = ++loadRevision;
        modelLoaded.store (false, std::memory_order_release);
        requestedModelFile = file;
        statusText = "Loading " + file.getFileNameWithoutExtension() + "...";

        // Assigning the revision and enqueueing under the same lock preserves
        // request order when several load requests arrive close together.
        loaderPool.addJob ([this, file, revision]
        {
            if (auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
                job != nullptr && job->shouldExit())
            {
                return;
            }

            {
                const juce::ScopedLock stateGuard (stateLock);

                if (revision != loadRevision)
                    return;
            }

            // Parsing/model construction is deliberately kept off the audio thread.
            const auto loaded = namLoader.loadNAMFile (file);
            const auto error = loaded ? juce::String {} : namLoader.getLastError();

            if (auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
                job != nullptr && job->shouldExit())
            {
                return;
            }

            auto stale = false;

            {
                const juce::ScopedLock stateGuard (stateLock);

                stale = revision != loadRevision;

                if (! stale)
                {
                    modelLoaded.store (loaded, std::memory_order_release);
                    statusText = loaded ? "Loaded " + file.getFileNameWithoutExtension()
                                        : "Bypass: " + error;
                }
            }

            // A newer request owns the desired state; discard this obsolete result.
            if (stale)
                namLoader.clear();
        });
    }
}

void NAMProcessor::onPrepared (double sampleRate, int samplesPerBlock)
{
    if (! namLoader.prepare (sampleRate, samplesPerBlock))
        clearLoadedModel ("Bypass: " + namLoader.getLastError());
}

// Applies a Neural Amp Modeler network to the incoming audio signal.
// NAM models reproduce the nonlinear behaviour of guitar amplifiers and
// related audio equipment.
void NAMProcessor::processAudio (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (! modelLoaded.load (std::memory_order_acquire) || buffer.getNumChannels() <= 0)
        return;

    const auto inDb = inputParam != nullptr ? inputParam->load() : 0.0f;
    const auto outDb = outputParam != nullptr ? outputParam->load() : 0.0f;

    const auto inputGain = juce::Decibels::decibelsToGain (inDb);
    const auto outputGain = juce::Decibels::decibelsToGain (outDb);
    auto* monoOutput = buffer.getWritePointer (0);

    // NAM processing is mono internally, so all input channels are averaged
    // before the model is evaluated.
    // The loader chunks oversized host blocks internally without allocating.
    if (! namLoader.processInputBlock (buffer.getArrayOfReadPointers(),
                                       buffer.getNumChannels(),
                                       monoOutput,
                                       numSamples,
                                       inputGain))
    {
        return;
    }

    buffer.applyGain (0, 0, numSamples, outputGain);

    // Broadcast the processed mono signal to every output channel.
    for (int channel = 1; channel < buffer.getNumChannels(); ++channel)
        buffer.copyFrom (channel, 0, buffer, 0, 0, numSamples);
}

// Persist the requested path even while loading so a project save remains reproducible.
void NAMProcessor::writeExtraState (juce::ValueTree& state)
{
    const juce::ScopedLock lock (stateLock);
    state.setProperty ("modelPath", requestedModelFile.getFullPathName(), nullptr);
}

void NAMProcessor::restoreExtraState (const juce::ValueTree& state)
{
    const auto modelPath = state.getProperty ("modelPath").toString();

    if (modelPath.isNotEmpty())
    {
        loadNAMFileAsync (juce::File (modelPath));
        return;
    }

    clearLoadedModel ("No NAM model loaded");
}

void NAMProcessor::clearLoadedModel (juce::String reason)
{
    const juce::ScopedLock lock (stateLock);
    ++loadRevision;
    requestedModelFile = juce::File {};
    modelLoaded.store (false, std::memory_order_release);
    statusText = std::move (reason);
    namLoader.clear();
}
} // namespace better
