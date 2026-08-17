#include "audio/modules/IRCabProcessor.h"

#include <cmath>
#include <utility>

namespace better
{
namespace
{
constexpr auto gatewayReferenceSampleRate = 48000.0;
constexpr auto gatewayIRLevelDb = -18.0f;
} // namespace

IRCabProcessor::IRCabProcessor()
    : AudioModuleProcessor (createDescriptor())
{
    levelParam = getParameters().getRawParameterValue ("level");
}

IRCabProcessor::~IRCabProcessor()
{
    loaderPool.removeAllJobs (true, -1);
}

ModuleDescriptor IRCabProcessor::createDescriptor()
{
    return
    {
        "ir",
        "IR / Cab",
        "CAB",
        {
            toggleControl ("enabled", "Enabled", true),
            gainSlider ("level", "Level", 0.0f, -24.0f, 12.0f)
        },
        false,
        true
    };
}

juce::String IRCabProcessor::getStatusText() const
{
    const juce::ScopedLock lock (stateLock);
    return statusText;
}

float IRCabProcessor::calculateGatewayIRGain (double originalIRSampleRate) noexcept
{
    const auto validSampleRate = std::isfinite (originalIRSampleRate) && originalIRSampleRate > 0.0
                               ? originalIRSampleRate
                               : gatewayReferenceSampleRate;
    const auto referenceGain = juce::Decibels::decibelsToGain (gatewayIRLevelDb);
    return referenceGain * static_cast<float> (gatewayReferenceSampleRate / validSampleRate);
}

void IRCabProcessor::loadIRFileAsync (const juce::File& file)
{
    if (! file.existsAsFile())
    {
        clearLoadedIR ("No IR loaded");
        return;
    }

    std::uint64_t revision = 0;

    {
        const juce::ScopedLock lock (stateLock);
        revision = ++loadRevision;
        requestedIRFile = file;
        statusText = "Loading " + file.getFileNameWithoutExtension() + "...";

        loaderPool.addJob ([this, file, revision]
        {
            {
                const juce::ScopedLock stateGuard (stateLock);

                if (revision != loadRevision)
                    return;
            }

            auto ir = IRLoader::loadIRFile (file);

            if (auto* job = juce::ThreadPoolJob::getCurrentThreadPoolJob();
                job != nullptr && job->shouldExit())
            {
                return;
            }

            const auto loaded = ir != nullptr && ! ir->samples.empty();

            const juce::ScopedLock stateGuard (stateLock);

            if (revision != loadRevision)
                return;

            // Publish DSP data and metadata as one revision-checked operation.
            // Lock order is always stateLock -> convolutionMutex.
            if (loaded)
            {
                // JUCE already contributes originalIRRate / hostRate while
                // resampling. This remaining factor yields Gateway's total
                // -18 dB * 48000 / hostRate without double compensation.
                convolver.setImpulseResponse (ir->samples,
                                              ir->sampleRate,
                                              calculateGatewayIRGain (ir->sampleRate));
            }
            else
                convolver.clear();

            statusText = loaded ? "Loaded " + file.getFileNameWithoutExtension()
                                : "Bypass: IR could not be loaded";
        });
    }
}

void IRCabProcessor::onPrepared (double sampleRate, int samplesPerBlock)
{
    convolver.prepare (sampleRate,
                       juce::jmax (1, getTotalNumOutputChannels()),
                       samplesPerBlock);
}

void IRCabProcessor::onReset()
{
    convolver.reset();
}

void IRCabProcessor::processAudio (juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (! convolver.hasImpulseResponse() || ! convolver.process (buffer, numSamples))
        return;

    const auto levelDb = levelParam != nullptr ? levelParam->load() : 0.0f;
    const auto levelGain = juce::Decibels::decibelsToGain (levelDb);

    if (std::abs (levelGain - 1.0f) < 0.0001f)
        return;

    buffer.applyGain (0, numSamples, levelGain);
}

void IRCabProcessor::writeExtraState (juce::ValueTree& state)
{
    const juce::ScopedLock lock (stateLock);
    state.setProperty ("irPath", requestedIRFile.getFullPathName(), nullptr);
}

void IRCabProcessor::restoreExtraState (const juce::ValueTree& state)
{
    const auto irPath = state.getProperty ("irPath").toString();

    if (irPath.isNotEmpty())
    {
        loadIRFileAsync (juce::File (irPath));
        return;
    }

    clearLoadedIR ("No IR loaded");
}

void IRCabProcessor::clearLoadedIR (juce::String reason)
{
    const juce::ScopedLock lock (stateLock);
    ++loadRevision;
    requestedIRFile = juce::File {};
    statusText = std::move (reason);
    convolver.clear();
}
} // namespace better
